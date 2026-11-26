#pragma once
// In-game Pack Builder (Phase 3a): the JSON the build reads (PackBuilder/menus/*.json) and writes (MRCustom_P.json), with
// Python's rules: objects keep their key order (json.loads gives a dict in file order), numbers stay int or float as written,
// and Dump() prints exactly what json.dumps(value, indent=2) prints (ensure_ascii, float repr). Pure: tests/ingame_pack_core_tests.cpp.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace builder::pack::json
{
	enum class Type { Null, Bool, Int, Float, String, Array, Object };

	struct Value
	{
		Type                     type = Type::Null;
		bool                     b = false;
		long long                i = 0;
		double                   f = 0;
		std::string              s;
		std::vector<std::string> keys;    // Object: keys[i] names items[i]
		std::vector<Value>       items;   // Array items or Object values

		const Value* Find(std::string_view a_key) const
		{
			if (type != Type::Object) return nullptr;
			for (size_t k = 0; k < keys.size(); ++k)
				if (keys[k] == a_key) return &items[k];
			return nullptr;
		}
		double Number() const { return type == Type::Int ? static_cast<double>(i) : type == Type::Float ? f : 0.0; }
		bool   IsNumber() const { return type == Type::Int || type == Type::Float; }
	};

	inline Value MakeString(std::string a_s)
	{
		Value v;
		v.type = Type::String;
		v.s = std::move(a_s);
		return v;
	}
	inline Value MakeFloat(double a_f)
	{
		Value v;
		v.type = Type::Float;
		v.f = a_f;
		return v;
	}
	inline Value MakeBool(bool a_b)
	{
		Value v;
		v.type = Type::Bool;
		v.b = a_b;
		return v;
	}
	inline Value MakeArray()
	{
		Value v;
		v.type = Type::Array;
		return v;
	}
	inline Value MakeObject()
	{
		Value v;
		v.type = Type::Object;
		return v;
	}
	inline void Set(Value& a_object, std::string a_key, Value a_value)   // dict[key] = value (an existing key keeps its place)
	{
		for (size_t k = 0; k < a_object.keys.size(); ++k)
			if (a_object.keys[k] == a_key) {
				a_object.items[k] = std::move(a_value);
				return;
			}
		a_object.keys.push_back(std::move(a_key));
		a_object.items.push_back(std::move(a_value));
	}

	// repr(float): shortest round-trip digits; fixed form for exponents -4..15 (whole numbers keep ".0"), else d.ddde+XX
	inline std::string PyRepr(double a_v)
	{
		if (std::isnan(a_v)) return "NaN";
		if (std::isinf(a_v)) return a_v > 0 ? "Infinity" : "-Infinity";
		char       buffer[64];
		const auto end = std::to_chars(buffer, buffer + sizeof(buffer), a_v, std::chars_format::scientific).ptr;
		std::string s(buffer, end), sign;
		if (s[0] == '-') {
			sign = "-";
			s.erase(0, 1);
		}
		const auto  e = s.find('e');
		const int   exponent = std::stoi(s.substr(e + 1));
		std::string digits = s.substr(0, e);
		if (digits.size() > 1) digits.erase(1, 1);   // the '.'
		if (exponent >= -4 && exponent < 16) {
			if (exponent < 0) return sign + "0." + std::string(static_cast<size_t>(-exponent - 1), '0') + digits;
			const size_t whole = static_cast<size_t>(exponent) + 1;
			if (digits.size() <= whole) return sign + digits + std::string(whole - digits.size(), '0') + ".0";
			return sign + digits.substr(0, whole) + "." + digits.substr(whole);
		}
		std::string out = sign + digits.substr(0, 1);
		if (digits.size() > 1) out += "." + digits.substr(1);
		const int magnitude = exponent < 0 ? -exponent : exponent;
		out += exponent < 0 ? "e-" : "e+";
		if (magnitude < 10) out += "0";
		return out + std::to_string(magnitude);
	}

	namespace detail
	{
		struct Reader
		{
			std::string_view t;
			size_t           p = 0;
			std::string      error;

			void Skip()
			{
				while (p < t.size() && (t[p] == ' ' || t[p] == '\t' || t[p] == '\n' || t[p] == '\r')) ++p;
			}
			bool Fail(const char* a_what)
			{
				if (error.empty()) error = std::string(a_what) + " at character " + std::to_string(p);
				return false;
			}
			static void Utf8(std::string& a_out, std::uint32_t c)
			{
				if (c < 0x80) a_out += static_cast<char>(c);
				else if (c < 0x800) {
					a_out += static_cast<char>(0xC0 | (c >> 6));
					a_out += static_cast<char>(0x80 | (c & 0x3F));
				} else if (c < 0x10000) {
					a_out += static_cast<char>(0xE0 | (c >> 12));
					a_out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
					a_out += static_cast<char>(0x80 | (c & 0x3F));
				} else {
					a_out += static_cast<char>(0xF0 | (c >> 18));
					a_out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
					a_out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
					a_out += static_cast<char>(0x80 | (c & 0x3F));
				}
			}
			bool Hex4(std::uint32_t& a_out)
			{
				if (p + 4 > t.size()) return Fail("short \\u escape");
				a_out = 0;
				for (int k = 0; k < 4; ++k) {
					const char c = t[p++];
					a_out <<= 4;
					if (c >= '0' && c <= '9') a_out |= static_cast<std::uint32_t>(c - '0');
					else if (c >= 'a' && c <= 'f') a_out |= static_cast<std::uint32_t>(c - 'a' + 10);
					else if (c >= 'A' && c <= 'F') a_out |= static_cast<std::uint32_t>(c - 'A' + 10);
					else return Fail("bad \\u escape");
				}
				return true;
			}
			bool String(std::string& a_out)
			{
				++p;   // the opening quote
				while (p < t.size()) {
					const char c = t[p++];
					if (c == '"') return true;
					if (static_cast<unsigned char>(c) < 0x20) return Fail("control character in a string");
					if (c != '\\') {
						a_out += c;
						continue;
					}
					if (p >= t.size()) break;
					const char e = t[p++];
					switch (e) {
					case '"': a_out += '"'; break;
					case '\\': a_out += '\\'; break;
					case '/': a_out += '/'; break;
					case 'b': a_out += '\b'; break;
					case 'f': a_out += '\f'; break;
					case 'n': a_out += '\n'; break;
					case 'r': a_out += '\r'; break;
					case 't': a_out += '\t'; break;
					case 'u': {
						std::uint32_t c1 = 0;
						if (!Hex4(c1)) return false;
						if (c1 >= 0xD800 && c1 < 0xDC00 && p + 1 < t.size() && t[p] == '\\' && t[p + 1] == 'u') {
							const size_t back = p;
							p += 2;
							std::uint32_t c2 = 0;
							if (!Hex4(c2)) return false;
							if (c2 >= 0xDC00 && c2 < 0xE000) c1 = 0x10000 + ((c1 - 0xD800) << 10) + (c2 - 0xDC00);
							else p = back;
						}
						Utf8(a_out, c1);
						break;
					}
					default: return Fail("bad escape");
					}
				}
				return Fail("unterminated string");
			}
			bool Number(Value& a_out)
			{
				const size_t start = p;
				bool         isFloat = false;
				if (p < t.size() && t[p] == '-') ++p;
				while (p < t.size() && ((t[p] >= '0' && t[p] <= '9') || t[p] == '.' || t[p] == 'e' || t[p] == 'E' || t[p] == '+' || t[p] == '-')) {
					if (t[p] == '.' || t[p] == 'e' || t[p] == 'E') isFloat = true;
					++p;
				}
				const auto text = t.substr(start, p - start);
				if (isFloat) {
					a_out.type = Type::Float;
					const auto r = std::from_chars(text.data(), text.data() + text.size(), a_out.f);
					if (r.ec != std::errc() || r.ptr != text.data() + text.size()) return Fail("bad number");
				} else {
					a_out.type = Type::Int;
					const auto r = std::from_chars(text.data(), text.data() + text.size(), a_out.i);
					if (r.ec != std::errc() || r.ptr != text.data() + text.size()) return Fail("bad number");
				}
				return true;
			}
			bool Word(std::string_view a_word)
			{
				if (t.substr(p, a_word.size()) != a_word) return Fail("unexpected text");
				p += a_word.size();
				return true;
			}
			bool Parse(Value& a_out, int a_depth)
			{
				if (a_depth > 64) return Fail("too deep");
				Skip();
				if (p >= t.size()) return Fail("unexpected end");
				const char c = t[p];
				if (c == '{') {
					a_out = MakeObject();
					++p;
					Skip();
					if (p < t.size() && t[p] == '}') {
						++p;
						return true;
					}
					for (;;) {
						Skip();
						if (p >= t.size() || t[p] != '"') return Fail("expected a key");
						std::string key;
						if (!String(key)) return false;
						Skip();
						if (p >= t.size() || t[p] != ':') return Fail("expected ':'");
						++p;
						Value v;
						if (!Parse(v, a_depth + 1)) return false;
						Set(a_out, std::move(key), std::move(v));   // a repeated key: the last one wins (json.loads)
						Skip();
						if (p < t.size() && t[p] == ',') {
							++p;
							continue;
						}
						if (p < t.size() && t[p] == '}') {
							++p;
							return true;
						}
						return Fail("expected ',' or '}'");
					}
				}
				if (c == '[') {
					a_out = MakeArray();
					++p;
					Skip();
					if (p < t.size() && t[p] == ']') {
						++p;
						return true;
					}
					for (;;) {
						Value v;
						if (!Parse(v, a_depth + 1)) return false;
						a_out.items.push_back(std::move(v));
						Skip();
						if (p < t.size() && t[p] == ',') {
							++p;
							continue;
						}
						if (p < t.size() && t[p] == ']') {
							++p;
							return true;
						}
						return Fail("expected ',' or ']'");
					}
				}
				if (c == '"') {
					a_out.type = Type::String;
					return String(a_out.s);
				}
				if (c == 't') {
					a_out = MakeBool(true);
					return Word("true");
				}
				if (c == 'f') {
					a_out = MakeBool(false);
					return Word("false");
				}
				if (c == 'n') {
					a_out = Value{};
					return Word("null");
				}
				if (c == '-' || (c >= '0' && c <= '9')) return Number(a_out);
				return Fail("unexpected character");
			}
		};

		inline void Quote(std::string& a_out, const std::string& a_s)   // ensure_ascii=True
		{
			a_out += '"';
			char buffer[16];
			for (size_t k = 0; k < a_s.size();) {
				const auto c = static_cast<unsigned char>(a_s[k]);
				std::uint32_t code = c;
				size_t        len = 1;
				if (c >= 0xF0 && k + 3 < a_s.size() + 0) {
					code = ((c & 0x07u) << 18) | ((a_s[k + 1] & 0x3Fu) << 12) | ((a_s[k + 2] & 0x3Fu) << 6) | (a_s[k + 3] & 0x3Fu);
					len = 4;
				} else if (c >= 0xE0 && k + 2 < a_s.size()) {
					code = ((c & 0x0Fu) << 12) | ((a_s[k + 1] & 0x3Fu) << 6) | (a_s[k + 2] & 0x3Fu);
					len = 3;
				} else if (c >= 0xC0 && k + 1 < a_s.size()) {
					code = ((c & 0x1Fu) << 6) | (a_s[k + 1] & 0x3Fu);
					len = 2;
				}
				k += len;
				switch (code) {
				case '"': a_out += "\\\""; continue;
				case '\\': a_out += "\\\\"; continue;
				case '\n': a_out += "\\n"; continue;
				case '\r': a_out += "\\r"; continue;
				case '\t': a_out += "\\t"; continue;
				case '\b': a_out += "\\b"; continue;
				case '\f': a_out += "\\f"; continue;
				default: break;
				}
				if (code >= 0x20 && code < 0x7F) {
					a_out += static_cast<char>(code);
				} else if (code < 0x10000) {
					std::snprintf(buffer, sizeof(buffer), "\\u%04x", code);
					a_out += buffer;
				} else {
					const std::uint32_t v = code - 0x10000;
					std::snprintf(buffer, sizeof(buffer), "\\u%04x\\u%04x", 0xD800 + (v >> 10), 0xDC00 + (v & 0x3FF));
					a_out += buffer;
				}
			}
			a_out += '"';
		}

		inline void Dump(std::string& a_out, const Value& a_v, int a_level)
		{
			const std::string inner(static_cast<size_t>(2 * (a_level + 1)), ' '), outer(static_cast<size_t>(2 * a_level), ' ');
			switch (a_v.type) {
			case Type::Null: a_out += "null"; return;
			case Type::Bool: a_out += a_v.b ? "true" : "false"; return;
			case Type::Int: a_out += std::to_string(a_v.i); return;
			case Type::Float: a_out += PyRepr(a_v.f); return;
			case Type::String: Quote(a_out, a_v.s); return;
			case Type::Array:
				if (a_v.items.empty()) {
					a_out += "[]";
					return;
				}
				a_out += "[\n";
				for (size_t k = 0; k < a_v.items.size(); ++k) {
					a_out += inner;
					Dump(a_out, a_v.items[k], a_level + 1);
					a_out += k + 1 < a_v.items.size() ? ",\n" : "\n";
				}
				a_out += outer + "]";
				return;
			case Type::Object:
				if (a_v.items.empty()) {
					a_out += "{}";
					return;
				}
				a_out += "{\n";
				for (size_t k = 0; k < a_v.items.size(); ++k) {
					a_out += inner;
					Quote(a_out, a_v.keys[k]);
					a_out += ": ";
					Dump(a_out, a_v.items[k], a_level + 1);
					a_out += k + 1 < a_v.items.size() ? ",\n" : "\n";
				}
				a_out += outer + "}";
				return;
			}
		}
	}

	inline bool Parse(std::string_view a_text, Value& a_out, std::string& a_error)
	{
		if (a_text.starts_with("\xEF\xBB\xBF")) a_text.remove_prefix(3);
		detail::Reader r{ a_text };
		if (!r.Parse(a_out, 0)) {
			a_error = r.error;
			return false;
		}
		r.Skip();
		if (r.p != a_text.size()) {
			a_error = "extra data at character " + std::to_string(r.p);
			return false;
		}
		return true;
	}

	inline std::string Dump(const Value& a_v)   // json.dumps(v, indent=2)
	{
		std::string out;
		detail::Dump(out, a_v, 0);
		return out;
	}
}
