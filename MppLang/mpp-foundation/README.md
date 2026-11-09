# MppFoundation

Parts of the swift-foundation fork (`MppLang/mpp-lib`), converted to M++ and
built by `mpp` as a module: `import MppFoundation`.

So far: **Decimal** (`Decimal`, `Decimal+Math`, `Decimal+Conformances`, and
`Decimal+Compatibility` with the `NSDecimal…` functions) and `ComparisonResult`.

The fork targets Swift 6.2; M++ builds with Swift 6.0, so a few things were
ported (`Portability.mpp`):
- `Span`/`MutableRawSpan`/`InlineArray` became unsafe buffers and arrays;
  `[unchecked:]` accesses are still bounds-checked in M++.
- Swift 6.0 types a large literal in a mixed comparison as `Int`, so those
  literals are written `(… as UInt128)`.
- There's no `Locale` yet: parsing takes `decimalSeparator:` (default `"."`).
- On macOS, programs that import MppFoundation build for macOS 15 (`UInt128`).

License: Apache 2.0 with Runtime Library Exception (LICENSE-swift-foundation.md).
Next candidates (self-contained in the fork): Base64, the JSON scanner/writer,
CharacterSet, then Data and Date.
