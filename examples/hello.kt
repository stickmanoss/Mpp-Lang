// MppxxKotlin: Kotlin compiled and run by mpp — `mpp examples/hello.kt`
import mpp.*

fun main() {
    print("Hello from Kotlin", Build.kotlinVersion, "on JDK", Build.jvm)
    val squares = (1..5).map { it * it }
    print("squares:", squares, "sum:", squares.fold(0) { a, b -> a add b })   // `add` stops on overflow
    print(3.0, 0.1, null)                                                        // 3.0 0.1 nil, like M++
}
