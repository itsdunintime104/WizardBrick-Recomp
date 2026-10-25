import CoreGraphics
// Prints "id x y w h" of the largest on-screen window owned by process ID argv[1].
let owner = Int(CommandLine.arguments[1]) ?? -1
let list = CGWindowListCopyWindowInfo([.optionOnScreenOnly], kCGNullWindowID) as? [[String: Any]] ?? []
var best: (Int, CGRect)? = nil
for w in list where (w[kCGWindowOwnerPID as String] as? Int) == owner {
    guard let n = w[kCGWindowNumber as String] as? Int,
          let b = w[kCGWindowBounds as String] as? [String: CGFloat] else { continue }
    let r = CGRect(x: b["X"] ?? 0, y: b["Y"] ?? 0, width: b["Width"] ?? 0, height: b["Height"] ?? 0)
    if best == nil || r.width * r.height > best!.1.width * best!.1.height { best = (n, r) }
}
if let (n, r) = best { print(n, Int(r.minX), Int(r.minY), Int(r.width), Int(r.height)) }
