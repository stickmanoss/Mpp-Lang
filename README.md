# M++

M++ is a Swift-syntax language that compiles to native binaries. Everything in this repository is written in M++: the `mpp` driver and front end, the `mpp pm` package manager, the bootstrap that builds `mpp` the first time, the UI engine, the shading library and every other library. `mpp` lowers `.mpp` source to Swift, and the Swift 6 toolchain (`swiftc`, with LLVM) type-checks it and generates machine code.

## Quick start

```sh
make                          # build bin/mpp (needs a Swift 6 toolchain)
bin/mpp examples/hello.mpp    # run a program (built and cached on first run)
make test                     # run every test
make install                  # put mpp on your PATH (/usr/local/bin)
```

Then try:

```sh
mpp examples/engine.mpp            # a UI app on MppUI's own engine
mpp examples/liquidglass-api.mpp   # Liquid Glass with SwiftUI's API, drawn by Metallic shaders
mpp examples/metallic.mpp          # Metal-style shaders on the CPU (writes metallic.png)
mpp examples/ncm.mpp               # Next-Concept Modeling: text in 7 passes instead of 32
mpp examples/hello.cpp             # C++ through mpp (MppxxCpp)
mpp examples/hello.kt              # Kotlin through mpp (MppxxKotlin)
```

## Commands

**Run and build**

| Command | What it does |
|---|---|
| `mpp file.mpp` | Build (optimized, cached) and run |
| `mpp file.mpp -- a b c` | Program arguments go after `--` |
| `mpp file.mpp "Title"` | Run in a window with that title (UI apps, or any program's output) |
| `mpp build file.mpp out/` | Build an optimized binary and place it at `out/file` |
| `mpp --debug file.mpp` | Unoptimized build with debug info (compiles faster) |
| `mpp -vm file.mpp` | Run Accelarate math on its fusing VM |

**Other languages** (same caching, same safety)

| Command | What it does |
|---|---|
| `mpp file.cpp` | C++ with M++'s I/O and safety checks ([MppxxCpp](#mppxxcpp-c-through-mpp)) |
| `mpp --std=c++23 file.cpp` | Pick the ISO C++ standard: 17, 20 (default), 23, 26 |
| `mpp file.kt` | Kotlin with M++'s I/O and checks ([MppxxKotlin](#mppxxkotlin-kotlin-through-mpp)) |
| `mpp --kotlin=2.2 file.kt` | Pick the Kotlin language version |
| `mpp --iso-strict file.cpp\|kt` | Warnings are errors |

**Packages** ([MppPM](#packages-mpppm-mpp-pm))

| Command | What it does |
|---|---|
| `mpp pm init` | Start a package (`Package.mpp`, `Sources/`, `Tests/`, `.mppgnore`) |
| `mpp pm build` / `mpp pm run` / `mpp pm test` | Build, run or test the package |
| `mpp pm add-dependency <url>` / `mpp pm update` | Add a dependency / update pinned versions |

**Tools**

| Command | What it does |
|---|---|
| `mpp highlight file.mpp` | Print with Xcode-style colors (`--html` for a web page) |
| `mpp --emit-swift file.mpp` | Show the Swift that M++ lowers to |
| `mpp --no-auto-parallel file.mpp` | Keep every loop sequential |
| `mpp --self-test` | Run the front end's own tests |
| `mpp --version` | Versions of `mpp`, swiftc, the C++ compiler and Kotlin |

**Environment variables**

| Variable | Effect |
|---|---|
| `MPP_CACHE` | Where builds are cached (default `~/.cache/mpp`) |
| `MPP_SWIFTC`, `MPP_CXX` | Use a specific swiftc / C++ compiler |
| `MPP_KOTLIN_HOME`, `JAVA_HOME` | Where Kotlin and the JDK are |
| `MPP_MACOS_TARGET` | macOS deployment target (default 14.0) |
| `MPP_UI_HEADLESS=out.png` | Render a UI app to a PNG instead of a window |
| `MPP_UI_SCRIPT="click 20 30; type hi"` | Drive a headless UI app (`down`/`up x y` hold the pointer) |
| `MPP_UI_STYLE=standard` | Plain Tahoe controls on the window background instead of the Liquid Glass backdrop |
| `MPP_UI_SCALE=2` | Render at Retina resolution |
| `MPP_VERBOSE=1` | Print every compiler command |
| `MPP_METALLIC_NATIVE=0` | Keep Metallic shaders in M++ instead of compiling them to C++ |
| `MPP_SHADER_DEBUG=1` | Print the C++ that Metallic shaders compile to, and why any stay in M++ |

Programs are cached; running an unchanged program checks file times only and then `exec`s the binary. Compiler errors point at your `.mpp` file and line.

## Libraries at a glance

| `import` | What you get |
|---|---|
| (always) `MppCore` | Fast I/O (`out`, `TokenReader`), files, memory tools, `place` |
| `MppKit` | `MppDictionary`/`MppSet`, `mppSort`/`radixSort`, containers, JSON, CLI, logging, matrices |
| `MppUI` | SwiftUI's API and look on M++'s own engine, on every platform (no SwiftUI needed). `MppUIKit` is its old name |
| `liquidglass` | Make the whole app Liquid Glass (liquidGL's lens on Metallic; identical on every Mac) |
| `Metallic` | Metal-style shaders and the engine that runs them |
| `MppData` | SwiftData: `@Model`, `#Predicate`, `ModelContainer` |
| `MppFoundation` | Parts of the swift-foundation fork, built as M++ (`Decimal`) |
| `Accelarate` | Array math with a fusing VM |
| `neural` | Neural networks with autograd, transformers, NCM, and partial loading (models bigger than RAM) |
| `MppTest` | `expect(...)` and a test runner (`mpp pm test`) |

```
MppLang/bootstrap/                  bootstrap.mpp: builds bin/mpp the first time
MppLang/driver/                     the mpp driver and front end
MppLang/MppPM/                      the package manager and the Package.mpp API
MppLang/mpp-core/, mpp-kit/         MppCore, MppTest, MppKit
MppLang/mpp-ui/                     MppUI and liquidglass (Portable), the engine (Engine), the output window (Tools)
MppLang/mpp-metallic/               Metallic
MppLang/mpp-data/, mpp-neural/, mpp-accelerate/, mpp-foundation/
MppLang/mpp-cpp/                    MppxxCpp: the C++ headers (<mpp.hpp>) and the runtime (in M++)
MppLang/mpp-kotlin/                 MppxxKotlin: the Kotlin library (mpp.kt)
MppLang/mpp-lib/                    the swift-foundation fork (source for MppFoundation)
MppLang/editors/                    syntax coloring: VS Code/TextMate grammar, SwiftUI symbol lists
```

## Install

You need `make`, a Swift 6 toolchain (`swiftc` on PATH, Xcode 16+ on a Mac, or `MPP_SWIFTC=/path/to/swiftc`), and `git` for remote packages. For C++ files, the Swift toolchain's own clang is used. For Kotlin files, install Kotlin and a JDK 17+ (`brew install kotlin`, or `npm i -g kotlin-compiler`).

## Speed without giving up safety

Optimized builds keep every Swift safety check: array bounds, integer overflow and exclusive access. Instead of removing checks, M++ removes the ones it can prove never fail.

**Proven-safe loops.** Every `for i in a..<b` or `a...b` (with or without `where`) is lowered so that:

- the loop counter steps without an overflow check, because it provably stays below the range's upper bound. This lets LLVM keep values in registers and unroll the loop;
- arrays indexed only as `a[i]`, `a[i + k]` or `a[i - k]` are bounds-checked once, before the loop. If that check passes, the body runs with the per-access checks proven redundant. If it fails, the same body runs with every access checked, so an out-of-range index still stops the program at exactly the same iteration as before.

The second rule only applies when nothing else in the body can reach the array: no closures, no calls except pure math (`min`, `sqrt`, `Double(...)`, ...), no other use of it, and no `return`, `throw` or `&`. A loop the compiler rejects after lowering is compiled again as written.

On a loop like `a[i] = a[i] &* 31 &+ a[i - 1] ^ r` this runs about 2.5× faster than plain `-O` (0.21 s vs 0.53 s), the same speed as `-Ounchecked`, with all checks kept.

**Automatic parallel loops.** A range loop whose iterations are independent runs on all cores. The front end checks the body conservatively: it may only write `array[i]` at exactly the loop index, does no I/O, and calls only functions declared in the same file, pure math and initializers. A function from another module could change shared state that the compiler can't see, so calling one keeps the loop sequential. Then Swift 6's data-race checking must prove the loop race-free, or it stays sequential. At run time, loops too short to benefit stay sequential. Write `serial for` or `parallel for` to choose yourself.

## Packages: MppPM (`mpp pm`)

MppPM (`mpp pm`; `mpp lib` still works) works like SwiftPM: `Package.mpp` is the manifest, `Sources/<Target>/` and `Tests/<Target>/` hold the code, `Package.resolved` pins versions, and `.build/` holds checkouts and build output.

```swift
// Package.mpp
import PackageDescription

let package = Package(
    name: "App",
    products: [
        .executable(name: "app", targets: ["App"]),
    ],
    dependencies: [
        .package(path: "../Geometry"),
        .package(url: "https://github.com/me/vecmath", from: "1.2.0"),
    ],
    targets: [
        .executableTarget(name: "App", dependencies: [
            "Geometry",
            .product(name: "VecMath", package: "vecmath"),
        ]),
        .testTarget(name: "AppTests", dependencies: ["App"]),
    ]
)
```

```sh
mpp pm init [--type library|executable|empty] [--name N]
mpp pm build [-c release] [--product P] [--target T] [--build-tests]
mpp pm run [-c release] [product] [-- args...]
mpp pm test [-c release] [--filter text]
mpp pm add-dependency <url|path> [--from 1.2.0 | --exact 1.2.0 | --up-to-next-minor-from 1.2.0 | --branch main | --revision <sha>]
mpp pm resolve [--only-use-versions-from-resolved-file]   # CI: fail if pins don't cover the manifest
mpp pm update [package...]
mpp pm show-dependencies [--format json]
mpp pm dump-package
mpp pm clean | reset
```

All of these accept `--package-path <dir>`.

**`.mppgnore`** lists files MppPM must not compile as part of a target, in `.gitignore` syntax: `Drafts/` (a folder anywhere), `/Scratch.mpp` (relative to the `.mppgnore`), `*.draft.mpp`, `**/Generated/**`, and `!keep.mpp` to re-include. Files from the package root down to the target's folder apply, and later lines win. `mpp pm init` creates one (and a `.gitignore`).

How it works:

- **Manifests are programs.** `Package.mpp` is compiled against `PackageDescription` and run. The result is cached by content, so a manifest is only recompiled when you edit it.
- **Requirements:**
  - `from:` allows anything up to the next major version.
  - `upToNextMinor:` allows anything up to the next minor version.
  - `exact:`, ranges such as `"1.2.0"..<"1.5.0"`, `branch:` and `revision:` work as in SwiftPM.
  - `add-dependency` without a requirement uses `from:` the newest tag.
- **Resolution:** remote packages are git repositories, and their versions are semver tags (with or without a leading `v`). The newest version that satisfies every package's requirements wins. If `Package.resolved` already pins a version that still fits, that version is kept until you run `mpp pm update`. When no version fits, the error lists who requires what, plus the available versions.
- **Targets:** each target is its own module, named after the target (`import Geometry`), and target names must be unique across the dependency graph. MppCore is imported automatically everywhere.
- **Tests:** in a test target, every top-level `func test...()` is a test. Use `expect`, `expectEqual`, `expectThrows` and `fail`, or throw `TestSkipped`. Failed expectations are reported with their `.mpp` file and line, and the test keeps running.
- **Rebuilds:** a module is rebuilt only when its sources, its dependencies, the flags or the toolchain change.

## `MppKit`: the C++ library greatest hits

`import MppKit` brings in Swift-syntax equivalents of ten popular C++ libraries. `mpp` builds it the first time a program imports it, then caches it.

| From C++ | In M++ |
|---|---|
| Eigen | `Matrix<T>` / `Mat`: `*` is the matrix product, plus `transposed`, `solve`, `inverse`, `determinant` (LU with partial pivoting), `hadamard`, block, broadcast, reductions. The product kernel is cache-blocked and vectorized, and large products split across cores. |
| {fmt} | `format("{:>8.2f} {:#x} {:,}", ...)` and `println(...)`: positional args, fill/align, sign, width, precision, `d x X o b e f g % s`. |
| nlohmann/json | `JSON` value with literals: `j["a"][0]`, `j.name = "x"` (dynamic members), auto-creating objects and arrays, ordered keys, exact integers. `JSON.parse` reports the line and column of errors. |
| spdlog | `logger.info("{} ready", name)`, levels, patterns (`%Y-%m-%d %H:%M:%S.%e [%l] %v`), and console (colored), file, rotating-file and memory sinks. Messages below the logger's level are never formatted. |
| Abseil | `MppDictionary` / `MppSet` (formerly `FlatHashMap` / `FlatHashSet`, which still work): Swiss table with 8-byte SWAR probing, one-multiply integer hashing and 8-bytes-at-a-time string hashing, keys next to values, `values(forKeys:)` batch lookups, Dictionary's API and copy-on-write. |
| Intel TBB | `Parallel.for`, `forChunks`, `map`, `reduce`, `sum`, `sort` across all cores. |
| Boost | `RingBuffer` (circular_buffer), `BitSet` (dynamic_bitset with popcount and set-bit iteration), `SmallArray` (8 elements stored inline before any heap allocation), plus `PCG32` / `Xoshiro256` random generators with `normal`, `exponential`, `poisson`, `bernoulli` and `weightedIndex` (Boost.Random). |
| `std::sort` / pdqsort | `xs.mppSort()` (pattern-defeating quicksort, any `Comparable`, unstable) and `xs.radixSort()` (integers and floating point, stable). On 5M values: `mppSort` 1.1–1.5× and `radixSort` 2.7–3.1× faster than the standard library's `sort()` (`benchmarks/sorting.mpp`). |
| range-v3 | `chunks(of:)`, `windows(of:)`, `adjacentPairs`, `cartesianProduct`, `uniqued`, `interspersed`, `scan`, `grouped(by:)`, `sorted(by: keyPath)`, `sum`, `product`. |
| CLI11 | `CommandLineParser`: options, flags (`-vvv` counts), positionals, `--x=1`, grouped short flags, generated `--help`. |
| Google Benchmark | `benchmark("name") { ... }` picks the iteration count automatically; `blackHole(value)` stops the optimizer from deleting the measured work. |

`TokenFile` stores token streams (for language models) in a memory-mapped file with a checksummed header. Opening a 1-billion-token file takes about 2 µs, and reserving space for 1 trillion tokens about 90 µs. `TokenWriter` fills tokens in parallel and publishes the file atomically. See `examples/mppkit.mpp`.

Measured (Linux x86-64, 2 cores, `-O`):

- **Matrix product:** 512×512 doubles run at about 11 GFLOP/s.
- **`MppDictionary` vs Swift's `Dictionary`** (`benchmarks/dictionary.mpp`, random lookups, half of them misses):

  | | Dictionary | MppDictionary |
  |---|---|---|
  | 1,000 Int keys (in cache) | 17 ns | 3.8 ns |
  | 4,000,000 Int keys (out of cache) | 80–90 ns | 29–34 ns |
  | 100,000 String keys | 75–90 ns | 53–64 ns |

  A lookup that misses the CPU cache waits about 80–100 ns for main memory; MppDictionary gets below that because independent lookups overlap their misses.

## `neural`: machine learning

`import neural` brings the features of popular Swift ML libraries: tensors with reverse-mode autodiff (`backward()`, `valueAndGrad`), layers (`Linear`, `Conv2d`, `LSTM`, `MultiHeadAttention`, `TransformerBlock`, normalization, dropout), losses, optimizers (SGD, Adam, AdamW, RMSprop) with schedules, `DataFrame`, `MLClassifier`/`MLRegressor`, safetensors loading and saving, and BPE tokenizers with text generation. It also re-exports MppKit.

**Next-Concept Modeling (NCM).** `NCMTokenizer` and `NCMGenerator` implement [Next-Concept Modeling](https://arxiv.org/abs/2610.14162) (Kumbong, Khare, … Mirhoseini, Ré; Stanford, 2026), which attacks the memory-bandwidth cost of generation. An autoregressive model reads every weight once per token. NCM writes text coarse-to-fine as a pyramid of K levels of discrete codes, each level one forward pass that predicts all of its codes in parallel, so the weights are read about K times instead of L.

- **The tokenizer** is a multiscale residual VQ-VAE. Level k pools the residual to ℓ_k vectors and snaps each one to a codebook entry, and the reconstructions add up. It trains with nested dropout, and its commitment and codebook losses use straight-through gradients.
- **The generator** predicts all codes of level k from the previous reconstruction, with the paper's level weighting.
- **`NCMBandwidth`** counts weight traffic: a 16 GB model writing 1,024 tokens reads 16,384 GB autoregressively and 192 GB with NCM (12 passes).

Where the paper's text gives no number, this version uses doubling level lengths (1, 2, 4 … L), average pooling and linear reconstruction. Prompt conditioning isn't implemented yet. `examples/ncm.mpp` trains a toy model that writes short phrases in 7 passes.

**Partial loading: using only the part of a model you need.** A model doesn't have to fit in RAM to run. Most steps use only part of it, so `neural` keeps the weights on disk (int4) and holds in RAM only the part in use. It is the strategy [Colibri](https://github.com/JustVugg/colibri) uses to run a 744B-parameter mixture-of-experts in about 25 GB; this is an independent M++ implementation, for any model. There are two access patterns:

- **Sparse (mixture-of-experts).** A layer runs only the few experts its router picks, so only those are read. `ExpertStore` keeps the experts in one file. `ExpertCache` keeps recently used ones in RAM under a byte budget (consecutive tokens mostly reuse them) and reads misses in parallel. `StreamingMoE` routes each token: softmax over the top-k, top-p pruning, optional shared experts.
- **Sequential (dense models).** Layers run one after another, so only the current one has to be in RAM. `PartStore` keeps named parts (each a list of matrices) in one file. `PartStream` runs them in order while the next part loads in the background, then lets each finished part go. Peak memory is about two parts.

```swift
import neural

// Dense: a model bigger than RAM, one layer at a time.
let stream = PartStream(store: try PartStore(path: "model.parts"), lookahead: 1)
try stream.forEach { name, weights in x = block(x, weights) }

// Sparse: only the experts the router picks.
let cache = ExpertCache(store: try ExpertStore(path: "model.experts"), budgetBytes: 12 << 30)
let moe = StreamingMoE(layer: 3, router: routerWeights, cache: cache, topK: 8, topP: 0.85)
let y = try moe.forward(x)
```

- **`QuantizedMatrix`** stores int4 weights in groups of 64, with one Float32 scale per group (4.5 bits per weight). Its matrix-vector product runs in parallel, and with a searched scale per group the error is about 6% per matrix.
- **Building the files:** `PartStore.write(to:names:weights:)` and `ExpertStore.write`/`ExpertStore.convert(checkpoint:to:…)` take one part or one tensor at a time, so a checkpoint never has to fit in RAM. The converter reads sharded safetensors in F32, F16, BF16, and FP8 E4M3 with 128×128 block scales.
- **What stays resident** (embeddings, attention, routers, norms) is up to you: anything you don't stream stays in RAM.
- **`ModelPlan`** estimates whether a model fits a machine and how fast it will run:

```swift
print(ModelPlan(totalParameters: 744e9, activeParameters: 40e9, residentParameters: 17e9, ramBytes: 25e9))
// resident in RAM: 9.5 GB; cache for streamed weights: 11.6 GB; on disk: 408.9 GB
// per token: 12.9 GB streamed, 3.8 GB from disk at 70% cache hits
// ≈ 0.77 tokens/s at 3.0 GB/s (disk-bound); fits
print(ModelPlan(totalParameters: 70e9, activeParameters: 70e9, residentParameters: 2e9, ramBytes: 16e9))
// … per token: 38.2 GB streamed, 25.7 GB from disk at 32% cache hits; ≈ 0.11 tokens/s …
```

Streamed results equal computing with the same weights in RAM, exactly. `tests/streaming.mpp` checks this for both patterns and checks the dense stream's peak memory. The pattern that pays off is sparse: a mixture-of-experts reads a few percent of its weights per token. A dense model reads all of its streamed weights for every token, so it runs at disk speed.

## `Accelarate`: array math, with a fusing VM

```swift
import Accelarate
let x = Tensor.random(count: 10_000_000, in: -1 ... 1)
let y = relu(x * 2 + 1) - sqrt(abs(x))
print(sum(y), mean(y * y), maxElement(y))
```

`Tensor` holds Float arrays of any shape: `+ - * /` with tensors or scalars (broadcasting a smaller shape across the leading dimensions), `sqrt exp log sin cos tanh abs relu sigmoid square pow clamp max min`, reductions (`sum mean maxElement minElement dot norm variance`), `matmul` (`•`), `transposed`, `reshaped`.

`mpp file.mpp` evaluates every operation at once (eager, like NumPy). `mpp -vm file.mpp` runs the same code on the Accelarate VM: operations only record an expression; when a value is needed, the VM compiles the whole expression into one program of dense instructions (multiply-adds and scale-and-shift pairs fused into single instructions), caches it, and streams the arrays through it in L1-sized blocks on every core. Set `MPP_VM_TRACE=1` to see the programs. On 16M floats (`benchmarks/accelarate.mpp`):

| | eager | VM | hand-written loop |
|---|---|---|---|
| `relu(a * b + c * 2 - 1) * 0.5 + sqrt(abs(a))` | 112 ms | 39 ms | 31 ms |
| `sum((a - b) * (a - b))` | 70 ms | 15 ms | 24 ms |

The VM can't make the processor itself faster: it removes the extra passes over memory and the temporary arrays that eager evaluation makes.

## `MppData`: SwiftData for M++

```swift
import MppData

@Model final class Trip {
    @Attribute(.unique) var name: String
    var days: Int
    @Relationship(deleteRule: .cascade) var stops: [Stop] = []
    init(name: String, days: Int) { self.name = name; self.days = days }
}
@Model final class Stop {
    var city: String
    init(city: String) { self.city = city }
}

let container = try ModelContainer(for: Trip.self, Stop.self)
let context = container.mainContext
context.insert(Trip(name: "Japan", days: 10))
try context.save()
let long = try context.fetch(FetchDescriptor<Trip>(predicate: #Predicate { $0.days > 7 },
                                                   sortBy: [SortDescriptor(\.name)]))
```

The front end expands `@Model` (encoding, decoding, unique attributes, cascading relationships) and `#Predicate` (a closure), the way Swift's macros do. You get `ModelContainer` (one atomically written store file, or in memory), `ModelContext` (`insert`, `delete`, `delete(model:where:)`, `save`, `rollback`, `hasChanges`, `fetch`, `fetchCount`), `FetchDescriptor` (predicate, sort, limit, offset, pending changes), an identity map (a record is one live object), upserts on `@Attribute(.unique)`, `@Transient`, and to-one, optional and to-many relationships. Stored properties can be numbers, `Bool`, `String`, optionals, arrays and `[String: …]` dictionaries of those, and enums declared `enum Kind: String, CaseIterable, ModelValue`.

## MppUI and `liquidglass`

MppUI is SwiftUI's API with Apple's current look, Liquid Glass (macOS Tahoe / iOS 26), drawn by M++'s own engine on every platform; see the next section. It doesn't use SwiftUI, even on macOS: the only Apple pieces it touches there are the window itself (AppKit) and the system font (San Francisco, through Core Text), so programs look the same as before. Set `MPP_UI_EMBEDDED_FONT=1` to use the bundled Inter font instead. Besides SwiftUI's views, MppUI adds:

- an `app(...)` entry point that works from a plain `.mpp` file; its window is Liquid Glass (a soft backdrop with glass controls), or plain Tahoe controls on the window background with `MPP_UI_STYLE=standard`,
- components that follow the app's style: `Card`, `MppButton`, `MppTextField`, `Bar`, `Pill`, and the `.mppSurface()` modifier.

**The controls follow Apple's:**

- **Switches** have a wide track and a capsule knob that slides on a spring. The track fades to the accent color, and while the switch is held the knob swells into a clear glass lens that bends the track underneath. Dragging across a switch flips it.
- **Slider knobs** are white capsules that turn into glass lenses while dragged. Stepped sliders get tick marks.
- **Segmented controls** are capsules. The selection glides to the chosen segment and turns to glass while held.
- **Buttons** are soft glass bezels that brighten and swell slightly when pressed. Large buttons are capsules. `.glass` and `.glassProminent` are real Liquid Glass (refraction, a frosted fill, specular light), and prominent buttons are tinted glass.
- **Pop-up buttons** show gray chevrons on the bezel, without the old blue box.
- **Text fields** have rounder corners and an animated accent focus ring.
- **Checkboxes and radio buttons** fade to the accent color.
- **Progress** is a thin capsule track, and the spinner is Apple's twelve spokes.
- **Sheets, alerts and menus** are frosted glass panels with Tahoe's large radii, and menus highlight rows as accent capsules.
- **Sizes and colors** follow Tahoe: regular controls are 26 points tall, and fills use Apple's system grays.

`examples/liquidglass-controls.mpp` shows them all. In a headless run, `MPP_UI_SCRIPT="down 290 147"` holds the pointer down, so a screenshot shows the pressed, glass state.

`import liquidglass` (or `import liquidglass.mpp`) goes further: every MppUI component draws with `glassEffect`.

```swift
import liquidglass.mpp

struct Counter: View {
    @State var count = 0
    var body: some View {
        Card {
            Text("Count: \(count)")
            MppButton("Add", systemImage: "plus") { count += 1 }
            Button("Plain button") { count += 10 }   // glass too
        }
        .padding()
    }
}

app("Glass Counter") { Counter() }
```

With liquidglass imported:

- MppUI components draw with `glassEffect`.
- Every plain `Button` gets the glass button style.
- The window content sits in a `GlassEffectContainer`, so nearby glass shapes blend together.
- `.liquidGlass(tint:cornerRadius:interactive:)` adds glass to any view.

The glass looks the same on every macOS version from 14 on, and on Linux. A file that imports `liquidglass` has its `import MppUI` dropped automatically, so liquidglass's `app` wins. `mpp` builds macOS programs with a deployment target of macOS 14 (change it with `MPP_MACOS_TARGET`). See `examples/glass.mpp`.

### MppUI's own engine

The engine implements SwiftUI's API itself, entirely in M++ (renderer, text, windows through X11 or AppKit, Liquid Glass through Metallic shaders), and draws like SwiftUI does on the Mac:

- **The look:** macOS text styles and colors, push buttons, prominent buttons, checkboxes, switches, sliders, rounded text fields with a focus ring, pop-up menus, segmented controls, radio groups, steppers, sheets, alerts and the macOS window background. On macOS it uses the system font (SF Pro) through Core Text. Elsewhere it uses Inter, the closest freely licensed font.
- **Views:**
  - `Text`, `Image(systemName:)`, `Label`
  - `Spacer`, `Divider`
  - `VStack`, `HStack`, `ZStack`, `Group`
  - `ScrollView`, `List`, `Form`, `Section`
  - `LazyVGrid` and `GridItem`
  - `NavigationStack` and `NavigationLink`, `TabView`, `GroupBox`, `DisclosureGroup`, `LabeledContent`, `ProgressView`
  - `Button`, `Toggle`, `TextField`, `SecureField`, `Slider`, `Stepper`, `Picker`, `Menu`
  - `Color`, `LinearGradient`
- **Drawing:**
  - shapes: `Rectangle`, `RoundedRectangle`, `Capsule`, `Circle`, `Ellipse`, and your own `Shape` with `path(in:)`
  - `Path` with lines, curves, arcs, rects and ellipses
  - `Canvas` with `GraphicsContext`
  - `GeometryReader`
- **Data flow:** `@State`, `@Binding`, `@Environment` (including `dismiss` and observable objects), `@StateObject`, `@ObservedObject`, `@EnvironmentObject`, `@Bindable`, `@FocusState`, `@AppStorage`, `ObservableObject`, and `@Observable` classes (where the toolchain ships the Observation library).
- **Interaction:** `.onTapGesture`, `.gesture(DragGesture/TapGesture/LongPressGesture)`, `.onHover`, `.onAppear`, `.onDisappear`, `.onChange`, `.onSubmit`, keyboard focus with Tab.
- **Presentation:** `.sheet`, `.alert`, `.confirmationDialog`, `.popover`, `.navigationTitle`.
- **Modifiers:** padding, frame, fixedSize, offset, background, overlay, border, foregroundStyle, font, tint, opacity, cornerRadius, clipShape, shadow, disabled, lineLimit, multilineTextAlignment, the `*Style` modifiers, glassEffect, and ViewModifier.

**Liquid Glass with SwiftUI's API.** The glass is a port of [liquidGL](https://github.com/naughtyduk/liquidGL)'s lens (MIT, © NaughtyDuk) to Metallic shaders.

- **The lens:**
  - Inside a bevel band, the window behind it is offset along the edge normal by `edge · refraction + edge¹⁰ · bevelDepth`, with rounded corners and the offset fading toward the centre.
  - Frost, chromatic aberration, a multiply tint, drifting specular lights, magnify, tilt and a soft shadow.
  - It refracts a snapshot of the page, like liquidGL. The snapshot covers only the part of the window the lens's bevel, refraction, tilt and magnification can reach.
- **The same on any machine.** It runs on the CPU, so every Mac, Intel or Apple silicon and any macOS version from 14 on, shows exactly the same glass as Linux. It doesn't rely on Apple's frameworks: no SwiftUI, Metal or Core Animation.

```swift
Text("Hi").padding().glassEffect(.regular.tint(.teal).interactive(), in: .capsule)
Text("Alien").padding().glassEffect(.alien.chromaticAberration(0.4), in: .rect(cornerRadius: 24))
Circle().frame(width: 96, height: 96).glassEffect(.clear.magnify(1.6), in: .circle)   // a magnifier
Button("Go") { }.buttonStyle(.glass)              // and .glassProminent, GlassEffectContainer { }
```

`Glass` has SwiftUI's `.regular` (liquidGL's demo default, a little frost), `.clear`, `.identity`, `.tint(_:)` and `.interactive()`, and liquidGL's presets `.alien`, `.pulse`, `.frosted` and `.edge`. Its controls are `refraction`, `bevel(depth:width:)`, `frost`, `chromaticAberration`, `specular`, `shadow`, `magnify` and `tilt(x:y:)`.

One difference from liquidGL: liquidGL averages 16 random samples for frost. Here the snapshot gets one Gaussian blur with the same spread (σ = half the frost radius), so frost is smooth rather than grainy, and a wider frost costs no more. See `examples/liquidglass-api.mpp`.

Controls animate on springs (knobs, selections, presses, focus rings). Animation modifiers on your own views are accepted, but those changes apply instantly. Headless renders settle every animation at once, so screenshots repeat. `MPP_UI_HEADLESS=out.png` renders without a window, and `MPP_UI_SCRIPT="click 20 30; type hi; key return; wait 200"` drives the app (see `tests/mppui.mpp`). `MPP_UI_SCALE=2` renders at Retina resolution.

## Metallic: Metal-style shaders in M++

`import Metallic` gives you a shading language with Metal's syntax and an engine with Metal's API. Shaders are M++ functions with Metal's attributes:

```swift
import Metallic

struct VertexOut { var position: float4 [[position]]; var uv: float2 }

@vertex func quad(_ id: uint [[vertex_id]], _ u: Params [[buffer(0)]]) -> VertexOut { ... }

@fragment func shade(_ v: VertexOut [[stage_in]],
                     _ tex: texture2d<float> [[texture(0)]],
                     _ s: sampler [[sampler(0)]]) -> float4 {
    float4(tex.sample(s, v.uv).rgb * 0.8, 1)
}

@kernel func invert(_ src: texture2d<float> [[texture(0)]], _ dst: texture2d<float> [[texture(1)]],
                    _ gid: uint2 [[thread_position_in_grid]]) {
    dst.write(float4(1 - src.read(gid).rgb, 1), gid)
}

let device = MetallicCreateSystemDefaultDevice()!
let pipeline = try device.makeComputePipelineState(function: invertFunction)
let buffer = device.makeCommandQueue()!.makeCommandBuffer()!
let encoder = buffer.makeComputeCommandEncoder()!
encoder.setComputePipelineState(pipeline)
encoder.setTexture(src, index: 0); encoder.setTexture(dst, index: 1)
encoder.dispatchThreads(MetallicSize(width: w, height: h), threadsPerThreadgroup: MetallicSize(width: 16, height: 16))
encoder.endEncoding(); buffer.commit()
```

- **Shader language:**
  - Types: `float`…`float4`, `half`…, `int2`…, `uint`…, `bool` (vectors are SIMD).
  - Constructors like `float4(rgb, 1)`, and swizzles (`.xy`, `.rgb`, `.zw`, …).
  - Functions: `dot`, `length`, `normalize`, `cross`, `mix`, `clamp`, `saturate`, `smoothstep`, `step`, `fract`, `reflect`, `refract`, `pow`, `sin`, …
  - `texture2d<float>` (`sample`, `read`, `write`, `get_width`) and `sampler(filter:address:)`.
- **The front end** turns `@kernel`/`@vertex`/`@fragment` and the `[[...]]` attributes into bindings. Each shader `name` gets a `nameFunction` to build pipelines with, and arguments are bound once per dispatch, not once per pixel. A struct with a `[[position]]` member gets perspective-correct interpolation (`[[flat]]` members aren't interpolated). A parameter without an attribute is a compile-time error.
- **The engine** follows Metal's API: device, command queue, command buffers, compute and render encoders, render pass descriptors with clear or load and blending, scissor rects, triangles and triangle strips. Kernels and fragments run on the CPU, spread over all cores in row bands. `texture.savePNG(path)` writes the result. `MetallicGaussianBlur(sigma:)` (like `MPSImageGaussianBlur`) blurs at the same cost for any radius.

**Shaders compile to C++23.** `mpp` translates `@kernel` and `@fragment` functions, the structs they use and the helper functions they call into C++. It compiles them with MppxxCpp (optimized, with its usual checks) and runs them from the engine. `float4` is a clang vector type, so arithmetic is SIMD and swizzles work as in Metal. As in Metal, `sin`/`cos`/`tan` use fast approximations (within about 1e-7), and denormals flush to zero while a shader runs. Some code stays in M++, where it runs as before: closures, optionals, strings, `switch`, bit operators (C gives them a different precedence), array buffers, and any file whose C++ doesn't compile. `nameFunction.isNative` says which way a shader went. `MPP_METALLIC_NATIVE=0` keeps every shader in M++, and `MPP_SHADER_DEBUG=1` prints the generated C++ and says why a shader stayed in M++.

**How Metallic is built.** The API, the shader front end and the rasterizer are M++. Through the MppxxCpp wrapper, two parts are C++23: the core (`MppLang/mpp-metallic/Core`: its thread pool, pixel-format conversion, blur and PNG) and the compiled shaders.

`mpp benchmarks/metallic.mpp`, compared with the previous engine (M++ shaders, M++ core). These are times in ms on a 2-core Linux VM, best of 5:

| | before | now | speed-up |
|---|---|---|---|
| compute kernel (`sin` per pixel), 2048² | 40 | 28 | 1.4× |
| 3×3 blur kernel, 2048² | 31 | 13 | 2.4× |
| textured quad (two bilinear samples), 2048² | 216 | 90 | 2.4× |
| Liquid Glass, clear, 800×240 lens | 45 | 13 | 3.4× |
| Liquid Glass, frosted, 800×240 lens | 117 | 14 | 8.4× |
| Liquid Glass, six frosted pills | 70 | 11 | 6.4× |

The glass times exclude drawing the backdrop. Most of the glass gains come from doing less work rather than faster work: the lens snapshots only the part of the window its bevel can reach, frost blurs once at half resolution instead of taking 16 samples per pixel, and liquidGL's five-tap softening is applied once to the snapshot.

On the GPU question: Metallic runs on the CPU everywhere; it doesn't translate shaders to Apple's Metal. See `examples/metallic.mpp` and `tests/metallic.mpp`.

## MppxxCpp: C++ through `mpp`

MppxxCpp lets you write C++ and compile it with `mpp`, with M++'s fast I/O, safety checks and low-level control. It has three parts:

- **The compiler.** `mpp` compiles C++ with the clang that ships inside the Swift toolchain M++ already uses (Apple's clang on macOS). C++ and M++ therefore share one LLVM.
- **The library.** `#include <mpp.hpp>` gives you M++'s I/O, memory tools and Swift-style names in C++ (`MppLang/mpp-cpp/include`).
- **Interop.** M++ code can call C++ directly: `import cpp "x.hpp"`, or C++ written inside the `.mpp` file in a `#cpp { }` block.

```cpp
// hello.cpp — run with `mpp hello.cpp`
#include <mpp.hpp>
using namespace mpp;

int main() {
    Array<Int> squares;
    for (Int i : range(1, 6)) squares.append(i * i);
    print("squares:", squares, "in", iso::name());   // squares: [1, 4, 9, 16, 25] in C++23
}
```

**Same caching as `.mpp` files.** `mpp build hello.cpp out/` places the binary. Programs are cached; an unchanged program starts with no compiler run. Each translation unit is rebuilt only when it, or a header it includes, changes.

**Safety is always on, in every build:**

- Out-of-bounds access to fixed arrays, signed overflow, division by zero, bad shifts and falling off the end of a non-`void` function stop the program. Each check is compiled as one trap instruction, with no runtime library.
- The C++ standard library checks its own preconditions, such as `vector[i]` and `front()` on an empty container.
- Local variables start zeroed, and the stack is protected.
- `mpp::Array` and `mpp::Span` check every index and report the file and line, like M++.
- A failure prints a Swift-style `Fatal error:` line, flushes buffered output (C++'s and C stdio's) first, and names the functions it happened near. This runtime is C++23 (`MppLang/mpp-cpp/runtime/runtime.cpp`) and installs itself before `main`. C++ programs are linked by the C++ compiler, with no Swift runtime (a small program is about 20 KB).

On a sort-and-scan benchmark plus a matrix multiply, the hardened build ran within measurement noise of plain `clang -O2`. As with M++, there is no flag that turns the checks off.

**ISO control:**

- `--std=c++17|20|23|26` picks the standard. The default is ISO C++23, with no GNU extensions.
- `--iso-strict` makes every conformance warning an error.
- A file can carry its own settings, for example `// mpp: --std=c++23 --iso-strict util.cpp -lz`. That line can also name other `.cpp` files, `-l`/`-L`/`-framework` link flags, and `-D`/`-I` options.
- In code, `mpp::iso::standard()`, `iso::name()`, `iso::pure` and `iso::strict` tell a file how it was built. `MPP_REQUIRE_ISO(23);` gives a clear error when the file is built with an older standard than it needs.

**The library (`#include <mpp.hpp>`):**

| | |
|---|---|
| `print(a, b, c)`, `write(out, ...)`, `printTo(err, ...)`, `describe(x)` | Swift-style output, written straight to the file descriptor through a 64 KiB buffer. Numbers use `to_chars`; there are no locales and no iostream sync. Arrays, optionals and any type with `description()` print as M++ does (`3.0`, `[1, 2]`, `Optional(5)`, `nil`). |
| `in.readInt()`, `readDouble()`, `readWord()`, `readLine()`, `readAll()` | Fast buffered input on the raw file descriptor. |
| `MappedFile`, `readFile`, `writeFile` (atomic), `IOError` | Memory-mapped reading (`forEachLine`), and writes that never leave half a file. |
| `Array<T>`, `Span<T>` | Checked containers with M++'s API (`append`, `removeLast`, `first`, `map`, `filter`, `reduce`, ...). `withProvenRange(a, b, body)` checks the range once and then runs the loop with no per-element checks. |
| `Arena`, `Box<T>`, `Shared<T>`, `MPP_DEFER { }` | A bump allocator that runs destructors on `reset()`, owned and shared pointers, and Swift's `defer`. |
| `Int`, `Double`, `String`, ..., `range(a, b)`, `through(a, b)` | Swift's names and ranges. `Int` is word-sized, so it is M++'s `Int` across the interop boundary. |
| `precondition`, `fatalError`, `unwrap`, `narrow<T>`, `add`/`multiply` | Checks with messages. `wrappingAdd` and friends are Swift's `&+`, `&-` and `&*`. |
| `parallelFor(n, body)`, `parallelReduce(n, init, map, combine)` | The explicit form of M++'s automatic parallel loops. |

Against C stdio and iostreams (`mpp benchmarks/cpp_io.cpp`; 10 million numbers on Linux x86-64):

| | MppxxCpp | `printf`/`scanf` | iostream |
|---|---|---|---|
| write (int and double) | 0.54 s | 2.01 s | 2.63 s |
| read (int) | 0.14 s | 0.49 s | 0.40 s |

**M++ calling C++:**

```swift
import cpp "fastmath.hpp"      // fastmath.cpp beside it is compiled and linked too

#cpp {
    inline mpp::Int collatzSteps(mpp::Int n) {
        mpp::Int steps = 0;
        while (n != 1) { n = n % 2 == 0 ? n / 2 : 3 * n + 1; steps += 1; }
        return steps;
    }
}

var stats = fastmath.Stats()           // C++ structs, methods, namespaces
stats.add(4)
print(collatzSteps(27), String(cpp: fastmath.greeting("Omar")))
```

- **The wrapper is C++23.** Everything `mpp` does with C++ is a C++23 library linked into `mpp` (`MppLang/mpp-cpp/wrapper`). That covers building C++ programs, `import cpp`/`#cpp` interop, and translating and compiling Metallic shaders. The rest of the driver is M++ and calls it through a small C interface (`MppLang/driver/Wrapper.mpp`). The bootstrap builds the wrapper with the system's clang before the driver.
- **How it's compiled.** The wrapper compiles C++ as C++23 (`--std=c++26` raises it), with MppxxCpp's safety flags. That covers the `.cpp` files and the `#cpp` blocks, which are checked as C++23. Each header becomes a module that M++ imports through Swift's C++ interoperability. Swift 6's importer can't read the C++23 standard library headers yet, so it reads headers as C++20; keep C++23-only features out of headers that M++ imports.
- **Errors.** Mistakes in a `#cpp` block point at the `.mpp` file's own lines.
- **Strings.** M++ strings pass to `const char*` parameters as they are, and `String(cpp:)` turns a `std::string` into an M++ string.
- **Output.** M++ and C++ output share one stream, so it appears in order.
- **Example.** See `examples/cpp/interop.mpp`.
- **Limits.**
  - Interop works for single-file programs (`mpp file.mpp`); MppPM packages can't contain C++ targets yet.
  - `#cpp` blocks must sit at the top level.
  - Mixing `mpp::in` with M++'s own `readLine()` on the same input isn't supported, because each buffers separately.

## MppxxKotlin: Kotlin through `mpp`

`mpp hello.kt` compiles Kotlin with the Kotlin compiler, caches the result, and runs it. The wrapper was first written in Kotlin and is now part of `mpp`, in M++ (`MppLang/driver/Kotlin.mpp`). `import mpp.*` brings M++'s runtime behavior to Kotlin (`MppLang/mpp-kotlin/lib/mpp.kt`):

```kotlin
import mpp.*

fun main() {
    val n = input.readInt() ?: 0                 // fast byte scanner, not Scanner
    print("sum:", (1..n).fold(0) { a, b -> a add b })   // `add` stops on overflow instead of wrapping
    print(3.0, 0.1, null, listOf(1, 2))           // 3.0 0.1 nil [1, 2], like M++
}
```

- **Output** goes through a 64 KiB buffer on the raw file descriptor: 3.3× faster than `println`, level with a hand-tuned `BufferedWriter`. **Input** is a byte scanner. There are also mapped files and atomic `writeFile`.
- **Safety:**
  - `fatalError` and `precondition` print M++'s `file:line: Fatal error:` message.
  - `add`, `sub`, `mul` and `toIntChecked` check for overflow (Kotlin's `+` wraps silently).
  - `Arena` gives off-heap memory with bounds-checked regions.
  - `parallelFor` runs a loop on all cores.
- **Start-up:** cached runs start in about 0.13–0.17 s (JVM class-data sharing, archived on the first run).
- **Options:** `--kotlin=2.x` and `--iso-strict` (warnings are errors), or `// mpp:` lines in the file. `mpp build file.kt dir/` writes a launcher.

Kotlin is found through `MPP_KOTLIN_HOME`, `KOTLIN_HOME`, `kotlinc` on PATH, or the npm package. Calling Kotlin from M++ isn't supported yet.

## MppFoundation: the Foundation fork in M++

`import MppFoundation` builds parts of the swift-foundation fork (`MppLang/mpp-lib`) as M++. So far that's **`Decimal`** (exact decimal arithmetic, parsing, formatting, rounding and the `NSDecimal…` functions) and `ComparisonResult`: about 3,500 lines of the fork.

```swift
import MppFoundation
let total = Decimal(string: "0.1")! + Decimal(string: "0.2")!   // exactly 0.3
```

The fork targets Swift 6.2, so a small portability layer stands in for `Span` and `InlineArray`. There's no `Locale` yet, and on macOS a program that imports MppFoundation targets macOS 15, because `Decimal` uses `UInt128`. Next candidates, which have few dependencies inside the fork: Base64, the JSON scanner and writer, `CharacterSet`, then `Data` and `Date`. See `MppLang/mpp-foundation/README.md`.

## Syntax coloring

- `mpp highlight file.mpp` prints source in Xcode's default colors (`--html` writes a page), and compiler errors quote your code in those colors in a terminal.
- `MppLang/editors/vscode` is a VS Code extension (a TextMate grammar, so it also works in Sublime Text, Nova and TextMate). Link it into `~/.vscode/extensions`.
- SwiftUI types and modifiers (`MppLang/editors/symbols`, collected from Apple's SwiftUI documentation), the standard library and the M++ libraries are colored as system symbols; `place`, `unplace`, `serial` and `parallel` are keywords.

## `place` (placement new)

The `place` keyword from the first version is still available inside M++ code:

```swift
let p = place buffer Point(x: 1, y: 2)   // C++: new (buffer) Point(1, 2)
unplace p                                 // C++: p->~Point()
```

The destination can be a raw or typed pointer, a buffer, an `Arena`, a `Pool`, or any type conforming to `PlacementTarget`. The alignment and size of the destination are checked before the value is built, in every build mode. `place` and `unplace` are contextual keywords, so they still work as ordinary names. See `examples/place.mpp`.

## MppCore

| Memory | |
|---|---|
| `Arena` | Bump allocator; one free for everything. |
| `Pool<T>` | Fixed slots tracked in a bitmap. A slot is found with one trailing-zero count and freed by setting a bit; releasing the same slot twice is caught. |
| `Unique<T>` | Move-only owner; using it after a move is a compile error. |
| `Memory.withStackBytes` / `withStackBuffer`, `Memory.allocate(bytes:alignment:)`, `Memory.bitCast` | Scoped stack memory, aligned heap memory, checked type punning. |

| I/O | |
|---|---|
| `File`, `FS` | Move-only files with typed errors (`throws(IOError)`). `FS.write` is atomic (temp file, sync, rename). |
| `BufferedWriter` | Integers are formatted directly into the buffer with no allocation. Standard streams flush at exit. |
| `TokenReader` | `next(Int.self)` parses integers 8 bytes at a time, and rejects malformed input or overflow instead of silently failing. |
| `ByteWriter` / `ByteReader` | Binary data with explicit byte order and bounds checks. |
| `MappedFile` | Read-only memory mapping. |

| Bits (public, for your own fast code) | |
|---|---|
| `Bits.digitCount(v)` | Decimal digit count from the leading-zero count (`bits * 1233 >> 12`, plus one compare). |
| `Bits.writeDigits(v, endingAt:)` | Two digits per division, from a 200-byte table stored in the binary's read-only data. |
| `Bits.digitRun(chunk)` | SWAR: how many of 8 bytes are ASCII digits, with no per-byte branches. |
| `Bits.parse8(chunk, count:)` | SWAR: up to 8 digits converted to an integer with 3 multiply-and-mask steps. |
| `Bits.pow10(n)` | 10ⁿ for 0 ≤ n ≤ 19. |

## Speed

`make bench` compares M++ with C++ iostreams (g++ -O2, `sync_with_stdio(false)`) on 5M integers. Best of 11 runs on Linux x86-64 with Swift 6.0.3:

| | C++ | M++ |
|---|---|---|
| write (`out.write(i)` vs `cout << i`) | 0.147 s | 0.085 s |
| read (`next(Int.self)` vs `cin >> x`) | 0.160 s | 0.099 s |

With the writer in a local variable instead of a top-level global, the write takes 0.023 s, because top-level globals pay an exclusivity check on each access. The output is byte-for-byte identical to the C++ version.

## Status

- `mpp` uses `swiftc` as its back end; M++ doesn't have its own code generator.
- **Untested on macOS** (the author's checks ran on Linux):
  - the engine's AppKit window and SF fonts (`Cocoa.mpp`, written against AppKit and Core Text)
  - Metallic on macOS
  - the C++/Kotlin paths with Xcode's clang and Homebrew's Kotlin
- The engine draws text about 20% slower than the old C++ engine did; shapes and glass are as fast or faster.
- MppUI no longer covers everything in SwiftUI: programs that used SwiftUI features beyond the engine's list (see "MppUI's own engine") need those features added to the engine.
- The engine doesn't animate yet; DatePicker, ColorPicker, Table and drag-and-drop aren't implemented.
- Test functions can't be `async` yet.
- MppFoundation has `Decimal` only; most of the fork (Data, Date, URL, JSON, …) isn't converted yet.
