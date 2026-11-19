// Сторінки PDF → PNG для перевірки картинкою (pdftoppm немає, brew не ставимо): swiftc -O pdf_page.swift -o /tmp/pdfpage; /tmp/pdfpage файл.pdf префікс 3 4
import PDFKit
import AppKit
let a = CommandLine.arguments
let doc = PDFDocument(url: URL(fileURLWithPath: a[1]))!
for n in a[3...] {
  let p = doc.page(at: Int(n)! - 1)!
  let b = p.bounds(for: .mediaBox); let sc: CGFloat = 1.4
  let img = NSImage(size: NSSize(width: b.width * sc, height: b.height * sc))
  img.lockFocus(); NSColor.white.set(); NSRect(origin: .zero, size: img.size).fill()
  let ctx = NSGraphicsContext.current!.cgContext; ctx.scaleBy(x: sc, y: sc); p.draw(with: .mediaBox, to: ctx); img.unlockFocus()
  let rep = NSBitmapImageRep(data: img.tiffRepresentation!)!
  try! rep.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: a[2] + "-p" + n + ".png"))
}
