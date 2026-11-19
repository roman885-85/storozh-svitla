// Картинка профілю бота Telegram (18.11.2026, власник: «оформи ... пиктограмму для бота для нашей церкви»).
// Логотип церкви «Відродження» (вектор, ~/Documents/church-intro/layers/layer_full.pdf) на темно-синьому тлі
// заставки (10,16,32), тепле світло за хрестом і жовтий кружок із блискавкою: «світло/електрика».
// Telegram обрізає фото профілю колом — усе важливе всередині кола.
//   swift tools/bot_avatar.swift <layer_full.pdf> <out.png> [bolt|bulb] [розмір]
import Foundation
import CoreGraphics
import ImageIO
import UniformTypeIdentifiers

let a = CommandLine.arguments
guard a.count >= 3 else { print("usage: bot_avatar <layer_full.pdf> <out.png> [bolt|bulb] [size]"); exit(2) }
let variant = a.count > 3 ? a[3] : "bolt"
let N = a.count > 4 ? Int(a[4])! : 640
let S = Double(N) / 640.0
guard let doc = CGPDFDocument(URL(fileURLWithPath: a[1]) as CFURL), let page = doc.page(at: 1) else { fatalError("no pdf") }
let cs = CGColorSpace(name: CGColorSpace.sRGB)!
func col(_ r: Double, _ g: Double, _ b: Double, _ al: Double = 1) -> CGColor { CGColor(colorSpace: cs, components: [r/255, g/255, b/255, al])! }
let ctx = CGContext(data: nil, width: N, height: N, bitsPerComponent: 8, bytesPerRow: 0, space: cs,
                    bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
ctx.setAllowsAntialiasing(true); ctx.setShouldAntialias(true); ctx.interpolationQuality = .high
ctx.scaleBy(x: S, y: S)                                  // далі — у координатах 640×640, y угору

// тло: радіальний градієнт синього, як у заставці
let bg = CGGradient(colorsSpace: cs, colors: [col(34, 52, 104), col(18, 28, 60), col(10, 16, 32)] as CFArray, locations: [0, 0.55, 1])!
ctx.drawRadialGradient(bg, startCenter: CGPoint(x: 320, y: 360), startRadius: 0, endCenter: CGPoint(x: 320, y: 320), endRadius: 470, options: [.drawsAfterEndLocation])

// логотип: рамка в PDF (pt, y угору) — глобус, книга, хрест
let box = CGRect(x: 15, y: 164, width: 98, height: 93)
let k = 4.25                                             // px на pt: ширина логотипа ≈ 416 px
let cx = 312.0, cy = 352.0                               // центр логотипа на картинці (трохи вище й лівіше — місце кружку)
// тепле світло за хрестом
let crossAt = CGPoint(x: cx + (63.54 - box.midX) * k, y: cy + (228.3 - box.midY) * k)
//  світло «екраном», а не звичайним накладанням: на синьому тлі звичайне давало сірий серпанок
let glow = CGGradient(colorsSpace: cs, colors: [col(255, 196, 92, 0.62), col(240, 150, 60, 0.22), col(240, 150, 60, 0)] as CFArray, locations: [0, 0.42, 1])!
ctx.saveGState(); ctx.setBlendMode(.screen)
ctx.drawRadialGradient(glow, startCenter: crossAt, startRadius: 0, endCenter: crossAt, endRadius: 240, options: [])
ctx.restoreGState()
ctx.saveGState()
ctx.translateBy(x: cx - box.midX * k, y: cy - box.midY * k)
ctx.scaleBy(x: k, y: k)
ctx.clip(to: box.insetBy(dx: -2, dy: -2))
ctx.drawPDFPage(page)
ctx.restoreGState()

// кружок «електрика» праворуч унизу (у межах кола профілю: центр 452,188 від низу, r 96 → до краю 640/2 запас)
let bc = CGPoint(x: 452, y: 182), br = 98.0
ctx.setFillColor(col(10, 16, 32)); ctx.fillEllipse(in: CGRect(x: bc.x - br - 12, y: bc.y - br - 12, width: 2 * (br + 12), height: 2 * (br + 12)))
let gold = CGGradient(colorsSpace: cs, colors: [col(255, 222, 89), col(255, 176, 0)] as CFArray, locations: [0, 1])!
ctx.saveGState()
ctx.addEllipse(in: CGRect(x: bc.x - br, y: bc.y - br, width: 2 * br, height: 2 * br)); ctx.clip()
ctx.drawLinearGradient(gold, start: CGPoint(x: bc.x, y: bc.y + br), end: CGPoint(x: bc.x, y: bc.y - br), options: [])
ctx.restoreGState()
ctx.setFillColor(col(10, 16, 32))
if variant == "bulb" {
  // лампочка: колба + цоколь
  ctx.fillEllipse(in: CGRect(x: bc.x - 40, y: bc.y - 6, width: 80, height: 80))
  ctx.fill(CGRect(x: bc.x - 22, y: bc.y - 34, width: 44, height: 34))
  ctx.setFillColor(col(255, 200, 40)); ctx.fill(CGRect(x: bc.x - 22, y: bc.y - 22, width: 44, height: 5)); ctx.fill(CGRect(x: bc.x - 22, y: bc.y - 12, width: 44, height: 5))
  ctx.setFillColor(col(10, 16, 32)); ctx.fill(CGRect(x: bc.x - 12, y: bc.y - 46, width: 24, height: 12))
} else {
  // блискавка (у межах кружка, y угору)
  let p: [(Double, Double)] = [(0.12, 0.62), (-0.40, -0.06), (-0.04, -0.06), (-0.16, -0.62), (0.40, 0.10), (0.04, 0.10), (0.26, 0.62)]
  ctx.beginPath()
  for (i, q) in p.enumerated() {
    let pt = CGPoint(x: bc.x + q.0 * br, y: bc.y + q.1 * br)
    if i == 0 { ctx.move(to: pt) } else { ctx.addLine(to: pt) }
  }
  ctx.closePath(); ctx.fillPath()
}

let img = ctx.makeImage()!
let dest = CGImageDestinationCreateWithURL(URL(fileURLWithPath: a[2]) as CFURL, UTType.png.identifier as CFString, 1, nil)!
CGImageDestinationAddImage(dest, img, nil); CGImageDestinationFinalize(dest)
print("wrote", a[2], N, variant)
