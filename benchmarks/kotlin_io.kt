// MppxxKotlin output vs println / buffered writer: `mpp benchmarks/kotlin_io.kt`
import mpp.*

fun timed(body: () -> Unit): Double { val t = System.nanoTime(); body(); return (System.nanoTime() - t) / 1e9 }

fun main() {
    val n = 5_000_000
    val devNull = java.io.FileOutputStream("/dev/null")
    val mppTime = timed { val o = Output(devNull); for (i in 0 until n) { o.write(i.toLong()); o.put(' '.code); o.value(i * 0.5); o.put('\n'.code) }; o.flush() }
    val stdTime = timed { val ps = java.io.PrintStream(java.io.FileOutputStream("/dev/null"), false); for (i in 0 until n) ps.println("$i ${i * 0.5}"); ps.flush() }
    val bufTime = timed { val w = java.io.FileOutputStream("/dev/null").bufferedWriter(); for (i in 0 until n) { w.write(i.toString()); w.write(" "); w.write((i * 0.5).toString()); w.write("\n") }; w.flush() }
    print("5M lines (int + double):  mpp", "%.2fs".format(mppTime), " println", "%.2fs".format(stdTime), " BufferedWriter", "%.2fs".format(bufTime))
}
