import Foundation
import CoreGraphics
import ImageIO

let output = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
let sourcePath = CommandLine.arguments[2]
var source: CGImage?
if sourcePath != "-" {
    guard let images = CGImageSourceCreateWithURL(URL(fileURLWithPath: sourcePath) as CFURL, nil) else {
        fatalError("Cannot read application icon: \(sourcePath)")
    }
    for index in 0..<CGImageSourceGetCount(images) {
        if let image = CGImageSourceCreateImageAtIndex(images, index, nil),
           source == nil || image.width > source!.width {
            source = image
        }
    }
    guard source != nil else { fatalError("No image in application icon: \(sourcePath)") }
}
for argument in CommandLine.arguments.dropFirst(3) {
    let size = Int(argument)!
    let context = CGContext(data: nil, width: size, height: size, bitsPerComponent: 8,
        bytesPerRow: size * 4, space: CGColorSpaceCreateDeviceRGB(),
        bitmapInfo: CGImageAlphaInfo.noneSkipLast.rawValue)!
    let rect = CGRect(x: 0, y: 0, width: size, height: size)
    context.setFillColor(CGColor(red: 0.08, green: 0.16, blue: 0.22, alpha: 1))
    context.fill(rect)
    if let image = source {
        context.interpolationQuality = .high
        context.draw(image, in: rect)
    } else {
        // An original geometric S for builds without imported game artwork.
        context.setFillColor(CGColor(red: 0.65, green: 0.88, blue: 0.95, alpha: 1))
        for (x, y, w, h) in [(0.25, 0.66, 0.50, 0.12), (0.25, 0.44, 0.12, 0.28),
                             (0.25, 0.44, 0.50, 0.12), (0.63, 0.22, 0.12, 0.28),
                             (0.25, 0.22, 0.50, 0.12)] {
            context.fill(CGRect(x: x * Double(size), y: y * Double(size),
                                width: w * Double(size), height: h * Double(size)))
        }
    }
    let url = output.appendingPathComponent("icon-\(size).png")
    let destination = CGImageDestinationCreateWithURL(url as CFURL, "public.png" as CFString, 1, nil)!
    CGImageDestinationAddImage(destination, context.makeImage()!, nil)
    guard CGImageDestinationFinalize(destination) else { fatalError("Cannot write icon: \(url.path)") }
}
