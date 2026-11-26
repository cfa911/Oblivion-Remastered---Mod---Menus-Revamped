// In-game Pack Builder Phase 3b: the C++ side of tests/ingame_pack_image_parity_tests.py. Runs every line of a job file
// (one Pillow-port operation each, file names relative to the current folder) and writes each result for the Python
// side to compare with Pillow byte for byte.
//   ingame_pack_image_parity_driver <job file>
// Image files (.pki): "PKI1", mode (0 L, 1 LA, 2 RGB, 3 RGBA), 3 zero bytes, width and height (uint32 LE), the packed pixels.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>
#include "../src/builder/PackImage.h"
#include "../src/builder/PackImageCodec.h"

using namespace builder::pack;

static Bytes ReadFile(const std::string& a_path)
{
	std::ifstream f(a_path, std::ios::binary);
	return Bytes(std::istreambuf_iterator<char>(f), {});
}

static void WriteFile(const std::string& a_path, const Bytes& a_data)
{
	std::ofstream f(a_path, std::ios::binary);
	f.write(reinterpret_cast<const char*>(a_data.data()), static_cast<std::streamsize>(a_data.size()));
}

static void WriteText(const std::string& a_path, const std::string& a_text) { WriteFile(a_path, Bytes(a_text.begin(), a_text.end())); }

static Image Load(const std::string& a_path)
{
	const auto d = ReadFile(a_path);
	if (d.size() < 16 || d[0] != 'P' || d[1] != 'K' || d[2] != 'I' || d[3] != '1') {
		std::fprintf(stderr, "bad image file %s\n", a_path.c_str());
		std::exit(2);
	}
	auto le = [&](std::size_t at) { return d[at] | (d[at + 1] << 8) | (d[at + 2] << 16) | (std::uint32_t(d[at + 3]) << 24); };
	Image img{ static_cast<Mode>(d[4]), static_cast<int>(le(8)), static_cast<int>(le(12)), Bytes(d.begin() + 16, d.end()) };
	return img;
}

static void Save(const std::string& a_path, const Image& a_img)
{
	Bytes d = { 'P', 'K', 'I', '1', static_cast<std::uint8_t>(a_img.mode), 0, 0, 0 };
	for (std::uint32_t v : { static_cast<std::uint32_t>(a_img.width), static_cast<std::uint32_t>(a_img.height) })
		for (int s = 0; s < 32; s += 8) d.push_back(static_cast<std::uint8_t>(v >> s));
	d.insert(d.end(), a_img.px.begin(), a_img.px.end());
	WriteFile(a_path, d);
}

static Mode ModeOf(const std::string& a_name)
{
	return a_name == "L" ? Mode::L : a_name == "LA" ? Mode::LA : a_name == "RGB" ? Mode::RGB : Mode::RGBA;
}

static Resample FilterOf(const std::string& a_name)
{
	return a_name == "NEAREST" ? Resample::Nearest : a_name == "BOX" ? Resample::Box : a_name == "BILINEAR" ? Resample::Bilinear : Resample::Lanczos;
}

static float Radius(const std::string& a_text) { return static_cast<float>(std::strtod(a_text.c_str(), nullptr)); }   // Python float -> C float

int main(int argc, char** argv)
{
	if (argc < 2) return 2;
	std::ifstream jobs(argv[1]);
	std::string line;
	int done = 0;
	while (std::getline(jobs, line)) {
		std::istringstream in(line);
		std::vector<std::string> a;
		for (std::string w; in >> w;) a.push_back(w);
		if (a.empty()) continue;
		const std::string& op = a[0];
		auto i = [&](std::size_t n) { return std::stoi(a[n]); };
		if (op == "conv")
			Save(a[3], Convert(Load(a[1]), ModeOf(a[2])));
		else if (op == "resize")
			Save(a[5], Resize(Load(a[1]), i(2), i(3), FilterOf(a[4])));
		else if (op == "gauss")
			Save(a[3], GaussianBlur(Load(a[1]), Radius(a[2])));
		else if (op == "boxblur")
			Save(a[3], BoxBlur(Load(a[1]), Radius(a[2])));
		else if (op == "rank")
			Save(a[4], RankFilter(Load(a[1]), i(2), i(3)));
		else if (op == "mul")
			Save(a[3], Multiply(Load(a[1]), Load(a[2])));
		else if (op == "dark")
			Save(a[3], Darker(Load(a[1]), Load(a[2])));
		else if (op == "comp")
			Save(a[4], Composite(Load(a[1]), Load(a[2]), Load(a[3])));
		else if (op == "acomp")
			Save(a[3], AlphaComposite(Load(a[1]), Load(a[2])));
		else if (op == "acompat") {
			auto dst = Load(a[1]);
			AlphaCompositeAt(dst, Load(a[2]), i(3), i(4));
			Save(a[5], dst);
		} else if (op == "point") {
			std::array<std::uint8_t, 256> lut{};
			for (int v = 0; v < 256; ++v) lut[v] = static_cast<std::uint8_t>(std::stoi(a[2].substr(v * 2, 2), nullptr, 16));
			Save(a[3], Point(Load(a[1]), lut));
		} else if (op == "crop")
			Save(a[6], Crop(Load(a[1]), { i(2), i(3), i(4), i(5) }));
		else if (op == "paste") {
			auto dst = Load(a[1]);
			Paste(dst, Load(a[2]), i(3), i(4));
			Save(a[5], dst);
		} else if (op == "fill" || op == "rect") {
			auto dst = Load(a[1]);
			const Colour c = { static_cast<std::uint8_t>(i(6)), static_cast<std::uint8_t>(i(7)), static_cast<std::uint8_t>(i(8)), static_cast<std::uint8_t>(i(9)) };
			if (op == "fill")
				PasteFill(dst, { i(2), i(3), i(4), i(5) }, c);
			else
				DrawRectangle(dst, i(2), i(3), i(4), i(5), c);
			Save(a[10], dst);
		} else if (op == "putalpha") {
			auto img = Load(a[1]);
			PutAlpha(img, Load(a[2]));
			Save(a[3], img);
		} else if (op == "chan")
			Save(a[3], GetChannel(Load(a[1]), i(2)));
		else if (op == "merge") {
			std::vector<Image> bands;
			for (std::size_t n = 3; n < a.size(); ++n) bands.push_back(Load(a[n]));
			Save(a[2], Merge(ModeOf(a[1]), bands));
		} else if (op == "bbox") {
			const auto b = GetBBox(Load(a[1]));
			WriteText(a[2], b ? std::to_string(b->x0) + " " + std::to_string(b->y0) + " " + std::to_string(b->x1) + " " + std::to_string(b->y1) : "none");
		} else if (op == "extrema") {
			const auto e = GetExtrema(Load(a[1]));
			WriteText(a[2], std::to_string(e.first) + " " + std::to_string(e.second));
		} else if (op == "colors") {
			const auto c = GetColors(Load(a[1]), i(2));
			Bytes out;
			if (!c)
				out = { 'n', 'o', 'n', 'e' };
			else
				for (const auto& e : *c)
					for (std::uint32_t v : { e.count, e.pixel })
						for (int s = 0; s < 32; s += 8) out.push_back(static_cast<std::uint8_t>(v >> s));
			WriteFile(a[3], out);
		} else if (op == "bc")
			WriteFile(a[3], EncodeBc(Load(a[1]), a[2] == "DXT1" ? BcFormat::DXT1 : BcFormat::DXT5));
		else if (op == "dds") {
			const auto img = DecodeDds(ReadFile(a[1]));
			if (img)
				Save(a[2], *img);
			else
				WriteFile(a[2], {});
		} else if (op == "png") {
			const auto img = DecodePng(ReadFile(a[1]));
			if (img)
				Save(a[2], *img);
			else
				WriteFile(a[2], {});
		} else if (op == "raw") {   // raw <bytes file> <mode> <w> <h> <rawmode> <out>
			const auto d = ReadFile(a[1]);
			Save(a[6], FromRaw(ModeOf(a[2]), i(3), i(4), d.data(), a[5]));
		} else if (op == "tobytes")
			WriteFile(a[3], ToRaw(Load(a[1]), a[2]));
		else {
			std::fprintf(stderr, "unknown op %s\n", op.c_str());
			return 2;
		}
		++done;
	}
	std::printf("%d jobs\n", done);
	return 0;
}
