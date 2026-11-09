// MppxxKotlin tests (part of `make test` when Kotlin is installed).
import mpp.*

var failures = 0
fun check(cond: Boolean, what: String) { if (!cond) { printErr("FAIL:", what); failures++ } }

fun main() {
    check(describe(3.0) == "3.0", "3.0")
    check(describe(0.1) == "0.1", "0.1")
    check(describe(1e20) == "1e+20", "1e+20")
    check(describe(123456.0) == "123456.0", "123456.0")
    check(describe(Double.NaN) == "nan", "nan")

    check((2 add 3) == 5 && (6L mul 7L) == 42L, "checked arithmetic")
    check(7L.toIntChecked() == 7, "checked narrowing")

    val arena = Arena(256)
    val xs = arena.doubles(8)
    for (i in 0 until 8) xs[i] = i.toDouble()
    val ys = arena.longs(4)
    ys[3] = 99
    check(xs[7] == 7.0 && ys[3] == 99L && arena.bytesUsed == 96, "arena")
    arena.reset()
    check(arena.bytesUsed == 0, "arena reset")

    val path = "/tmp/mppxxkotlin-test-${ProcessHandle.current().pid()}.txt"
    writeFile(path, "one\ntwo\r\nthree")
    check(readFile(path) == "one\ntwo\r\nthree", "files")
    val lines = mutableListOf<String>()
    MappedFile(path).use { it.forEachLine { l -> lines += l } }
    check(lines == listOf("one", "two", "three"), "mapped lines")
    java.io.File(path).delete()

    val sums = LongArray(100_000)
    parallelFor(sums.size) { sums[it] = it.toLong() * 2 }
    check(sums[99_999] == 199_998L, "parallelFor")

    if (failures > 0) { printErr("MppxxKotlin:", failures, "failure(s)"); kotlin.system.exitProcess(1) }
    print("MppxxKotlin: all tests passed (Kotlin ${Build.kotlinVersion}, JDK ${Build.jvm})")
}
