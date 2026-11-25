// In-game Pack Builder Phase 3b: the Pillow image primitives in C++ (src/builder/PackImage.h, PackImageCodec.h), a port of
// the Pillow 13.0.0 operations the Python Pack Builder uses (tools/pack_builder/pipeline.py, charcoal_tabs.py).
// - fixed-point arithmetic checks worked out by hand (L24 grey, MULDIV255 premultiply, BLEND, the 565 truncation)
// - every op on small fixed inputs against Pillow's own output (tests/ingame_pack_image_expect.inc, written by
//   tests/ingame_pack_image_expect.py): convert, resize (LANCZOS, BILINEAR, BOX, NEAREST), GaussianBlur, BoxBlur, the
//   rank filters, multiply/darker, composite, alpha_composite, point, crop, paste, putalpha, rectangle, getbbox,
//   getextrema, getcolors (Pillow's table order), BC1/BC3 encode and decode, PNG decode (every filter, split IDAT)
// Byte parity on random and real stock inputs: tests/ingame_pack_image_parity_tests.py.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "../src/builder/PackImage.h"
#include "../src/builder/PackImageCodec.h"
#include "ingame_pack_image_expect.inc"

using namespace builder::pack;

unsigned checks = 0, failures = 0;
void Check(bool ok, const std::string& why)
{
	++checks;
	if (!ok) {
		++failures;
		std::printf("FAIL %s\n", why.c_str());
	}
}

static Bytes Lcg(std::uint32_t a_seed, std::size_t a_count)
{
	Bytes out(a_count);
	std::uint32_t x = a_seed;
	for (auto& b : out) {
		x = (x * 1103515245u + 12345u) & 0x7FFFFFFFu;
		b = static_cast<std::uint8_t>((x >> 16) & 255);
	}
	return out;
}

static Image Img(Mode a_mode, int a_w, int a_h, std::uint32_t a_seed)
{
	const auto raw = Lcg(a_seed, static_cast<std::size_t>(a_w) * a_h * BandCount(a_mode));
	return FromRaw(a_mode, a_w, a_h, raw.data(), RawOrder(a_mode));
}

static void Same(const Image& a_img, const std::uint8_t* a_exp, std::size_t a_size, const char* a_what)
{
	const auto raw = ToRaw(a_img, RawOrder(a_img.mode));
	bool ok = raw.size() == a_size && std::memcmp(raw.data(), a_exp, a_size) == 0;
	if (!ok && raw.size() == a_size) {
		for (std::size_t i = 0; i < a_size; ++i)
			if (raw[i] != a_exp[i]) {
				std::printf("  %s: first difference at byte %zu: %d, Pillow %d\n", a_what, i, raw[i], a_exp[i]);
				break;
			}
	}
	Check(ok, std::string(a_what) + " (" + std::to_string(raw.size()) + " bytes, Pillow " + std::to_string(a_size) + ")");
}

static void SameBytes(const Bytes& a_got, const std::uint8_t* a_exp, std::size_t a_size, const char* a_what)
{
	Check(a_got.size() == a_size && std::memcmp(a_got.data(), a_exp, a_size) == 0, a_what);
}

#define SAME(img, name) Same(img, k##name, k##name##Size, #name)

static Bytes DdsHeader(int a_w, int a_h, const char* a_fourcc, std::uint32_t a_dxgi = 0)
{
	Bytes h(128, 0);
	std::memcpy(h.data(), "DDS ", 4);
	auto put = [&](std::size_t at, std::uint32_t v) { std::memcpy(h.data() + at, &v, 4); };
	put(4, 124);
	put(8, 0x81007);
	put(12, static_cast<std::uint32_t>(a_h));
	put(16, static_cast<std::uint32_t>(a_w));
	put(76, 32);
	put(80, 4);
	std::memcpy(h.data() + 84, a_fourcc, 4);
	put(108, 0x1000);
	if (a_dxgi) {
		const std::uint32_t ext[5] = { a_dxgi, 3, 0, 1, 0 };
		h.insert(h.end(), reinterpret_cast<const std::uint8_t*>(ext), reinterpret_cast<const std::uint8_t*>(ext) + 20);
	}
	return h;
}

int main()
{
	// hand-worked arithmetic
	{
		Image px = NewImage(Mode::RGBA, 1, 1, { 10, 200, 30, 77 });
		const auto l = Convert(px, Mode::L);
		Check(l.px.size() == 1 && l.px[0] == ((10 * 19595 + 200 * 38470 + 30 * 7471 + 0x8000) >> 16), "RGBA->L is L24 rounded, alpha ignored");
		const auto m = Multiply(NewImage(Mode::L, 1, 1, { 200 }), NewImage(Mode::L, 1, 1, { 100 }));
		Check(m.px.size() == 1 && m.px[0] == 200 * 100 / 255, "multiply truncates a*b/255");
		const auto c = Composite(NewImage(Mode::L, 1, 1, { 255 }), NewImage(Mode::L, 1, 1, { 0 }), NewImage(Mode::L, 1, 1, { 128 }));
		Check(c.px.size() == 1 && c.px[0] == 128, "composite blends with DIV255 rounding");
		const auto bc = EncodeBc(NewImage(Mode::RGB, 4, 4, { 0xFF, 0xFF, 0xFF }), BcFormat::DXT1);
		Check(bc.size() == 8 && bc[0] == 0xFF && bc[1] == 0xFF, "BC1 encodes white as 565 0xFFFF");
		const auto bc2 = EncodeBc(NewImage(Mode::RGB, 4, 4, { 0x0F, 0x0F, 0x0F }), BcFormat::DXT1);
		Check(bc2.size() == 8 && bc2[0] == ((0x0F >> 3) | ((0x0F >> 2) << 5)) % 256, "BC1 565 truncates (v>>3, v>>2)");
		Check(EncodeBc(NewImage(Mode::RGBA, 5, 9, {}), BcFormat::DXT5).size() == 2 * 3 * 16, "BC3 pads to whole 4x4 blocks");
	}

	// convert
	SAME(Convert(Img(Mode::RGBA, 7, 3, 1), Mode::L), ConvRgbaL);
	SAME(Convert(Img(Mode::RGB, 5, 2, 2), Mode::L), ConvRgbL);
	SAME(Convert(Img(Mode::LA, 4, 2, 3), Mode::RGBA), ConvLaRgba);
	SAME(Convert(Img(Mode::L, 3, 2, 4), Mode::RGBA), ConvLRgba);
	SAME(Convert(Img(Mode::RGBA, 3, 2, 5), Mode::RGB), ConvRgbaRgb);
	// resize
	SAME(Resize(Img(Mode::RGBA, 13, 9, 4), 5, 4, Resample::Lanczos), ResLanczosRgba);
	SAME(Resize(Img(Mode::RGB, 3, 3, 5), 8, 7, Resample::Lanczos), ResLanczosRgb);
	SAME(Resize(Img(Mode::L, 17, 5, 6), 6, 11, Resample::Lanczos), ResLanczosL);
	SAME(Resize(Img(Mode::L, 9, 9, 7), 4, 4, Resample::Bilinear), ResBilinearDown);
	SAME(Resize(Img(Mode::L, 9, 9, 7), 15, 15, Resample::Bilinear), ResBilinearUp);
	SAME(Resize(Img(Mode::L, 9, 9, 7), 15, 4, Resample::Bilinear), ResVerticalFirstL);
	SAME(Resize(Img(Mode::RGBA, 13, 9, 4), 20, 3, Resample::Lanczos), ResVerticalFirstRgba);
	SAME(Resize(Img(Mode::L, 10, 12, 3), 8, 8, Resample::Bilinear), ResVerticalTie);
	SAME(Resize(Img(Mode::L, 16, 6, 8), 5, 1, Resample::Box), ResBox);
	SAME(Resize(Img(Mode::L, 5, 1, 9), 5, 4, Resample::Nearest), ResNearestA);
	SAME(Resize(Img(Mode::L, 3, 4, 10), 7, 2, Resample::Nearest), ResNearestB);
	// blurs and rank filters
	SAME(GaussianBlur(Img(Mode::L, 11, 9, 11), 1.2f), Gauss12);
	SAME(GaussianBlur(Img(Mode::L, 11, 9, 11), 3.0f), Gauss30);
	SAME(GaussianBlur(Img(Mode::L, 11, 9, 11), 0.6f), Gauss06);
	SAME(BoxBlur(Img(Mode::L, 10, 7, 12), 1.0f), Box1);
	SAME(BoxBlur(Img(Mode::L, 10, 7, 12), 4.0f), Box4);
	SAME(BoxBlur(Img(Mode::L, 10, 7, 12), 2.5f), Box25);
	SAME(BoxBlur(Img(Mode::L, 6, 5, 13), 8.0f), Box8Small);
	SAME(MedianFilter(Img(Mode::L, 8, 7, 14), 3), Median3);
	SAME(MaxFilter(Img(Mode::L, 8, 7, 14), 5), Max5);
	SAME(MinFilter(Img(Mode::L, 8, 7, 14), 3), Min3);
	SAME(RankFilter(Img(Mode::L, 8, 7, 14), 5, 7), Rank5);
	// chops, composites, paste
	SAME(Multiply(Img(Mode::L, 6, 4, 15), Img(Mode::L, 6, 4, 16)), Multiply);
	SAME(Darker(Img(Mode::L, 6, 4, 15), Img(Mode::L, 6, 4, 16)), Darker);
	SAME(Composite(Img(Mode::RGBA, 4, 3, 17), Img(Mode::RGBA, 4, 3, 18), Img(Mode::L, 4, 3, 19)), CompositeRgba);
	SAME(Composite(Img(Mode::RGB, 4, 3, 17), Img(Mode::RGB, 4, 3, 18), Img(Mode::L, 4, 3, 19)), CompositeRgb);
	SAME(AlphaComposite(Img(Mode::RGBA, 5, 4, 20), Img(Mode::RGBA, 5, 4, 21)), AlphaComp);
	{
		auto dst = Img(Mode::RGBA, 8, 6, 22);
		AlphaCompositeAt(dst, Img(Mode::RGBA, 5, 4, 23), 5, 3);
		SAME(dst, AlphaCompAt);
	}
	{
		std::array<std::uint8_t, 256> lut{};
		for (int v = 0; v < 256; ++v) lut[v] = static_cast<std::uint8_t>(255 - v);
		SAME(Point(Img(Mode::L, 5, 3, 24), lut), Point);
	}
	SAME(Crop(Img(Mode::L, 4, 3, 25), { -2, -1, 5, 4 }), Crop);
	{
		auto dst = Img(Mode::RGBA, 5, 4, 26);
		Paste(dst, Img(Mode::RGBA, 3, 3, 27), -1, 2);
		SAME(dst, Paste);
		auto pa = Img(Mode::RGBA, 4, 3, 28);
		PutAlpha(pa, Img(Mode::L, 4, 3, 29));
		SAME(pa, PutAlpha);
		auto r = Img(Mode::L, 6, 5, 30);
		DrawRectangle(r, 1, 2, 4, 3, { 77 });
		SAME(r, Rect);
		auto f = NewImage(Mode::L, 6, 5, { 0 });
		PasteFill(f, { 1, 1, 4, 3 }, { 255 });
		SAME(f, PasteFill);
	}
	// queries
	{
		auto sparse = NewImage(Mode::L, 9, 7, { 0 });
		if (sparse.px.size() == 63) {
			sparse.px[5 * 9 + 2] = 3;
			sparse.px[1 * 9 + 6] = 9;
		}
		const auto box = GetBBox(sparse);
		Check(box && box->x0 == kBBox[0] && box->y0 == kBBox[1] && box->x1 == kBBox[2] && box->y1 == kBBox[3], "getbbox");
		Check(!GetBBox(NewImage(Mode::L, 3, 3, { 0 })), "getbbox of an empty image is none");
		const auto ext = GetExtrema(Img(Mode::L, 5, 5, 31));
		Check(ext.first == kExtrema[0] && ext.second == kExtrema[1], "getextrema");
		const auto palette = std::vector<std::array<std::uint8_t, 4>>{ { 0, 0, 0, 0 }, { 255, 0, 0, 255 }, { 12, 200, 7, 128 },
			{ 12, 200, 7, 129 }, { 255, 255, 255, 255 }, { 1, 2, 3, 4 } };
		const auto picks = Lcg(32, 30);
		auto few = NewImage(Mode::RGBA, 6, 5, {});
		for (int i = 0; i < 30 && few.px.size() == 120; ++i) std::memcpy(few.px.data() + 4 * i, palette[picks[i] % palette.size()].data(), 4);
		const auto colours = GetColors(few, 1 << 24);
		Bytes flat;
		if (colours)
			for (const auto& c : *colours) {
				for (int s = 0; s < 32; s += 8) flat.push_back(static_cast<std::uint8_t>(c.count >> s));
				for (int b = 0; b < 4; ++b) flat.push_back(static_cast<std::uint8_t>(c.pixel >> (8 * b)));
			}
		SameBytes(flat, kColors, kColorsSize, "getcolors in Pillow's table order");
		Check(!GetColors(few, 3), "getcolors over maxcolors is none");
	}
	// BCn
	SameBytes(EncodeBc(Img(Mode::RGB, 8, 8, 33), BcFormat::DXT1), kBc1Rgb8, kBc1Rgb8Size, "BC1 encode 8x8 RGB");
	SameBytes(EncodeBc(Img(Mode::RGB, 6, 5, 34), BcFormat::DXT1), kBc1Rgb6x5, kBc1Rgb6x5Size, "BC1 encode 6x5 RGB (padded)");
	SameBytes(EncodeBc(NewImage(Mode::RGB, 4, 4, { 90, 90, 90 }), BcFormat::DXT1), kBc1Flat, kBc1FlatSize, "BC1 encode flat");
	SameBytes(EncodeBc(Img(Mode::RGBA, 8, 8, 35), BcFormat::DXT5), kBc3Rgba8, kBc3Rgba8Size, "BC3 encode 8x8 RGBA");
	SameBytes(EncodeBc(Img(Mode::RGBA, 2, 2, 36), BcFormat::DXT5), kBc3Rgba2, kBc3Rgba2Size, "BC3 encode 2x2 RGBA");
	SameBytes(EncodeBc(Img(Mode::RGBA, 1, 1, 37), BcFormat::DXT5), kBc3Rgba1, kBc3Rgba1Size, "BC3 encode 1x1 RGBA");
	SameBytes(EncodeBc(Img(Mode::RGB, 4, 4, 38), BcFormat::DXT5), kBc3Rgb4, kBc3Rgb4Size, "BC3 encode RGB (opaque alpha)");
	{
		auto file = DdsHeader(7, 5, "DXT1");
		file.insert(file.end(), kDecBc1In, kDecBc1In + kDecBc1InSize);
		const auto a = DecodeDds(file);
		Check(a.has_value(), "decode DXT1");
		if (a) SAME(*a, DecBc1);
		auto file10 = DdsHeader(7, 5, "DX10", 71);
		file10.insert(file10.end(), kDecBc1In, kDecBc1In + kDecBc1InSize);
		const auto b = DecodeDds(file10);
		Check(b.has_value(), "decode DX10 BC1");
		if (b) SAME(*b, DecBc1Dx10);
		auto file3 = DdsHeader(5, 6, "DX10", 77);
		file3.insert(file3.end(), kDecBc3In, kDecBc3In + kDecBc3InSize);
		const auto c = DecodeDds(file3);
		Check(c.has_value(), "decode DX10 BC3");
		if (c) SAME(*c, DecBc3);
		Check(!DecodeDds(Bytes(10, 0)), "a short file is refused");
		Check(!DecodeDds(DdsHeader(4, 4, "DX10", 98)), "an unported format (BC7) is refused");
	}
	// PNG
	{
		struct P { const std::uint8_t* file; std::size_t size; const std::uint8_t* exp; std::size_t expSize; Mode mode; const char* name; };
		const P pngs[] = { { kPngLaFile, kPngLaFileSize, kPngLa, kPngLaSize, Mode::LA, "PNG LA" },
			{ kPngRgbaFile, kPngRgbaFileSize, kPngRgba, kPngRgbaSize, Mode::RGBA, "PNG RGBA" },
			{ kPngRgbFile, kPngRgbFileSize, kPngRgb, kPngRgbSize, Mode::RGB, "PNG RGB" },
			{ kPngLFile, kPngLFileSize, kPngL, kPngLSize, Mode::L, "PNG L" },
			{ kPngPillowFile, kPngPillowFileSize, kPngPillow, kPngPillowSize, Mode::RGBA, "PNG written by Pillow" } };
		for (const auto& p : pngs) {
			const auto img = DecodePng(Bytes(p.file, p.file + p.size));
			Check(img && img->mode == p.mode, std::string(p.name) + " mode");
			if (img) Same(*img, p.exp, p.expSize, p.name);
		}
		Bytes broken(kPngRgbaFile, kPngRgbaFile + kPngRgbaFileSize);
		broken[broken.size() - 20] ^= 0x55;
		const auto damaged = DecodePng(broken);
		Check(!damaged || damaged->px.size() == 4 * 5 * 4, "a damaged PNG never overruns");
		Check(!DecodePng(Bytes(8, 0)), "not a PNG is refused");
	}

	std::printf("%u checks, %u failures\n", checks, failures);
	return failures ? 1 : 0;
}
