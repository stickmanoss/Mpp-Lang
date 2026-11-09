// MppxxKotlin — Kotlin with M++'s I/O speed, safety and low-level control.
//
//   import mpp.*
//   fun main() {
//       val n = input.readInt() ?: 0
//       print("sum:", (1..n).sumOf { it.toLong() })
//   }
//
// Run with `mpp hello.kt`. Output is one 64 KiB buffer on the raw file
// descriptor (flushed at exit, before reading input, and before a fatal
// error); input is a byte scanner, not a Scanner/regex. Arithmetic that
// Kotlin would wrap silently has checked forms (`a add b`), and `Arena`
// gives off-heap memory with bounds-checked access.
@file:JvmName("Mpp")
package mpp

import java.io.FileDescriptor
import java.io.FileInputStream
import java.io.FileOutputStream
import java.io.IOException
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.channels.FileChannel
import java.nio.charset.StandardCharsets
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.StandardCopyOption
import java.nio.file.StandardOpenOption

// MARK: Output

/** A buffered output stream on a file descriptor. Thread-safe per call. */
class Output(private val stream: FileOutputStream, private val lineBuffered: Boolean = false) {
    private val buf = ByteArray(64 * 1024)
    private var n = 0
    private val digits = ByteArray(24)

    @Synchronized fun write(bytes: ByteArray, off: Int = 0, len: Int = bytes.size) {
        if (len >= buf.size) { flushLocked(); stream.write(bytes, off, len); return }
        if (n + len > buf.size) flushLocked()
        System.arraycopy(bytes, off, buf, n, len)
        n += len
    }
    @Synchronized fun put(b: Int) { if (n == buf.size) flushLocked(); buf[n++] = b.toByte() }
    @Synchronized fun write(s: String) {
        // ASCII fast path; everything else through UTF-8.
        var ascii = true
        for (c in s) if (c.code >= 0x80) { ascii = false; break }
        if (!ascii) { write(s.toByteArray(StandardCharsets.UTF_8)); return }
        if (n + s.length > buf.size) flushLocked()
        if (s.length >= buf.size) { stream.write(s.toByteArray(StandardCharsets.ISO_8859_1)); return }
        for (c in s) buf[n++] = c.code.toByte()
    }
    @Synchronized fun write(v: Long) {
        if (v == Long.MIN_VALUE) { write("-9223372036854775808"); return }
        var x = if (v < 0) -v else v
        var i = digits.size
        do { digits[--i] = ('0'.code + (x % 10).toInt()).toByte(); x /= 10 } while (x != 0L)
        if (v < 0) digits[--i] = '-'.code.toByte()
        write(digits, i, digits.size - i)
    }
    @Synchronized fun flush() = flushLocked()
    private fun flushLocked() { if (n > 0) { stream.write(buf, 0, n); n = 0 }; if (lineBuffered) stream.flush() }

    internal fun endPrint() { if (lineBuffered) flush() }

    /** Writes a value the way M++'s `print` does. */
    fun value(v: Any?) {
        when (v) {
            null -> write("nil")
            is Int -> write(v.toLong())
            is Long -> write(v)
            is Short -> write(v.toLong())
            is Byte -> write(v.toLong())
            is String -> write(v)
            is Double -> write(describe(v))
            is Float -> write(describe(v.toDouble()))
            is Boolean -> write(if (v) "true" else "false")
            is Char -> write(v.toString())
            is IntArray -> list(v.asList())
            is LongArray -> list(v.asList())
            is DoubleArray -> list(v.asList())
            is Array<*> -> list(v.asList())
            is Collection<*> -> list(v)
            else -> write(v.toString())
        }
    }
    private fun list(items: Collection<*>) {
        put('['.code)
        var first = true
        for (x in items) {
            if (!first) write(", ")
            first = false
            if (x is String) { put('"'.code); write(x); put('"'.code) } else value(x)
        }
        put(']'.code)
    }
}

/** Standard output and standard error. */
@JvmField val out = Output(FileOutputStream(FileDescriptor.out))
@JvmField val err = Output(FileOutputStream(FileDescriptor.err), lineBuffered = true)

private val flushAtExit = Runtime.getRuntime().addShutdownHook(Thread { out.flush() })

/** M++'s `print(a, b, c)`: values separated by spaces, then a newline. */
fun print(vararg values: Any?) {
    flushAtExit
    synchronized(out) {
        values.forEachIndexed { i, v -> if (i > 0) out.put(' '.code); out.value(v) }
        out.put('\n'.code)
    }
}

/** `print` to standard error. */
fun printErr(vararg values: Any?) {
    synchronized(err) {
        values.forEachIndexed { i, v -> if (i > 0) err.put(' '.code); err.value(v) }
        err.put('\n'.code)
        err.endPrint()
    }
}

/** The text `print` shows for a Double: 3.0, 0.1, 1e+20, like M++. */
fun describe(v: Double): String {
    if (v.isNaN()) return "nan"
    if (v.isInfinite()) return if (v > 0) "inf" else "-inf"
    val s = v.toString()                               // shortest round-trip, like Swift
    val e = s.indexOf('E')
    if (e < 0) return s
    val exp = s.substring(e + 1).toInt()
    if (exp in -4..15) return java.math.BigDecimal(s).toPlainString().let { if ('.' in it) it.trimEnd('0').trimEnd('.').let { t -> if ('.' in t) t else "$t.0" } else "$it.0" }
    val mant = s.substring(0, e).removeSuffix(".0")
    return mant + "e" + (if (exp >= 0) "+" else "-") + Math.abs(exp).toString().padStart(2, '0')
}

// MARK: Input

/** A fast byte scanner on a file descriptor. */
class Input(private val stream: FileInputStream) {
    private val buf = ByteArray(64 * 1024)
    private var pos = 0
    private var len = 0
    private var eof = false

    private fun fill(): Boolean {
        if (eof) return false
        out.flush()
        len = stream.read(buf, 0, buf.size)
        pos = 0
        if (len <= 0) { len = 0; eof = true; return false }
        return true
    }
    private fun skipSpace(): Boolean {
        while (true) {
            while (pos < len && buf[pos] <= ' '.code.toByte()) pos++
            if (pos < len) return true
            if (!fill()) return false
        }
    }

    /** The next integer, or null at end of input. */
    fun readInt(): Long? {
        if (!skipSpace()) return null
        var neg = false
        if (buf[pos] == '-'.code.toByte()) { neg = true; pos++ }
        var v = 0L
        var any = false
        while (true) {
            if (pos == len && !fill()) break
            val c = buf[pos] - '0'.code.toByte()
            if (c !in 0..9) break
            v = Math.addExact(Math.multiplyExact(v, 10L), c.toLong())
            any = true
            pos++
        }
        if (!any) fatalError("Expected an integer in input")
        return if (neg) -v else v
    }

    /** The next whitespace-separated word, or null at end of input. */
    fun readWord(): String? {
        if (!skipSpace()) return null
        val sb = StringBuilder()
        while (true) {
            if (pos == len && !fill()) break
            val c = buf[pos]
            if (c <= ' '.code.toByte()) break
            sb.append(c.toInt().toChar())
            pos++
        }
        return sb.toString()
    }

    fun readDouble(): Double? = readWord()?.let { it.toDoubleOrNull() ?: fatalError("Expected a number in input") }

    /** The next line without its newline, or null at end of input. */
    fun readLine(): String? {
        if (pos == len && !fill()) return null
        val bytes = java.io.ByteArrayOutputStream()
        while (true) {
            if (pos == len && !fill()) break
            var i = pos
            while (i < len && buf[i] != '\n'.code.toByte()) i++
            bytes.write(buf, pos, i - pos)
            if (i < len) { pos = i + 1; break }
            pos = len
        }
        val s = bytes.toString(StandardCharsets.UTF_8)
        return if (s.endsWith("\r")) s.dropLast(1) else s
    }
}

@JvmField val input = Input(FileInputStream(FileDescriptor.`in`))

// MARK: Files

/** The whole file (UTF-8). */
fun readFile(path: String): String = String(Files.readAllBytes(Path.of(path)), StandardCharsets.UTF_8)

/** Writes atomically: beside the target, then renamed over it. */
fun writeFile(path: String, text: String) {
    val target = Path.of(path)
    val tmp = Path.of("$path.mpp-tmp")
    Files.write(tmp, text.toByteArray(StandardCharsets.UTF_8))
    Files.move(tmp, target, StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE)
}

/** A file mapped into memory (read-only, no copy). */
class MappedFile(path: String) : AutoCloseable {
    private val channel = FileChannel.open(Path.of(path), StandardOpenOption.READ)
    val bytes: ByteBuffer = channel.map(FileChannel.MapMode.READ_ONLY, 0, channel.size())
    val count: Long get() = channel.size()
    /** Calls `body` with each line (without the newline). */
    fun forEachLine(body: (String) -> Unit) {
        val b = bytes.duplicate()
        val line = java.io.ByteArrayOutputStream()
        while (b.hasRemaining()) {
            val c = b.get()
            if (c == '\n'.code.toByte()) { body(line.toString(StandardCharsets.UTF_8).removeSuffix("\r")); line.reset() } else line.write(c.toInt())
        }
        if (line.size() > 0) body(line.toString(StandardCharsets.UTF_8))
    }
    override fun close() = channel.close()
}

// MARK: Safety

/** Stops the program like M++: `file:line: Fatal error: message`. */
fun fatalError(message: String = ""): Nothing {
    out.flush()
    val at = Throwable().stackTrace.firstOrNull { !it.className.startsWith("mpp.") }
    val where = at?.let { "${it.fileName}:${it.lineNumber}: " } ?: ""
    System.err.println(where + "Fatal error" + (if (message.isEmpty()) "" else ": $message"))
    System.err.flush()
    Runtime.getRuntime().halt(132)                      // like a trap: no finally blocks, no shutdown hooks
    throw IllegalStateException()                       // unreachable
}

/** Always checked, in every build. */
inline fun precondition(condition: Boolean, message: () -> String = { "Precondition failed" }) {
    if (!condition) fatalError(message())
}

/** `x!!` with M++'s message. */
fun <T : Any> T?.unwrap(message: String = "Unexpectedly found nil while unwrapping an Optional value"): T = this ?: fatalError(message)

/** Overflow-checked arithmetic (Kotlin's `+` wraps silently; M++'s stops). */
infix fun Int.add(o: Int): Int = try { Math.addExact(this, o) } catch (e: ArithmeticException) { fatalError("Arithmetic overflow") }
infix fun Int.sub(o: Int): Int = try { Math.subtractExact(this, o) } catch (e: ArithmeticException) { fatalError("Arithmetic overflow") }
infix fun Int.mul(o: Int): Int = try { Math.multiplyExact(this, o) } catch (e: ArithmeticException) { fatalError("Arithmetic overflow") }
infix fun Long.add(o: Long): Long = try { Math.addExact(this, o) } catch (e: ArithmeticException) { fatalError("Arithmetic overflow") }
infix fun Long.sub(o: Long): Long = try { Math.subtractExact(this, o) } catch (e: ArithmeticException) { fatalError("Arithmetic overflow") }
infix fun Long.mul(o: Long): Long = try { Math.multiplyExact(this, o) } catch (e: ArithmeticException) { fatalError("Arithmetic overflow") }
/** Checked narrowing (`Int32(x)` in M++). */
fun Long.toIntChecked(): Int = try { Math.toIntExact(this) } catch (e: ArithmeticException) { fatalError("Integer value doesn't fit in Int") }

// MARK: Low-level memory

/**
 * Off-heap memory: one allocation, bump-allocated regions, freed together.
 * Every access is bounds-checked (the JVM checks; out-of-range stops with
 * M++'s message instead of an exception).
 */
class Arena(capacityBytes: Int = 1 shl 20) {
    private val memory: ByteBuffer = ByteBuffer.allocateDirect(capacityBytes).order(ByteOrder.nativeOrder())
    private var top = 0
    val bytesUsed: Int get() = top

    /** A region of `count` doubles. */
    fun doubles(count: Int): DoubleRegion = DoubleRegion(slice(count * 8, 8).asDoubleBuffer(), count)
    /** A region of `count` longs. */
    fun longs(count: Int): LongRegion = LongRegion(slice(count * 8, 8).asLongBuffer(), count)
    /** A region of `count` bytes. */
    fun bytes(count: Int): ByteBuffer = slice(count, 1)

    private fun slice(bytes: Int, align: Int): ByteBuffer {
        val start = (top + align - 1) / align * align
        if (bytes < 0 || start + bytes > memory.capacity()) fatalError("Arena is full (${memory.capacity()} bytes)")
        top = start + bytes
        return memory.duplicate().position(start).limit(start + bytes).slice().order(ByteOrder.nativeOrder())
    }
    /** Frees everything at once. */
    fun reset() { top = 0 }
}

class DoubleRegion(private val b: java.nio.DoubleBuffer, val count: Int) {
    operator fun get(i: Int): Double { if (i !in 0 until count) fatalError("Index out of range"); return b.get(i) }
    operator fun set(i: Int, v: Double) { if (i !in 0 until count) fatalError("Index out of range"); b.put(i, v) }
}
class LongRegion(private val b: java.nio.LongBuffer, val count: Int) {
    operator fun get(i: Int): Long { if (i !in 0 until count) fatalError("Index out of range"); return b.get(i) }
    operator fun set(i: Int, v: Long) { if (i !in 0 until count) fatalError("Index out of range"); b.put(i, v) }
}

// MARK: Parallel

/** Calls `body(i)` for every i in 0 until count, spread over all cores. */
fun parallelFor(count: Int, grain: Int = 4096, body: (Int) -> Unit) {
    val cores = Runtime.getRuntime().availableProcessors()
    if (count < grain * 2 || cores == 1) { for (i in 0 until count) body(i); return }
    java.util.stream.IntStream.range(0, count).parallel().forEach { body(it) }
}

/** How this file was built (Kotlin's version of MppxxCpp's `iso`). */
object Build {
    val kotlinVersion: String get() = KotlinVersion.CURRENT.toString()
    val jvm: String get() = System.getProperty("java.version")
    val strict: Boolean get() = System.getProperty("mpp.strict") == "1"
}
