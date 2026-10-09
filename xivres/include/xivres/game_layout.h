#ifndef XIVRES_GAME_LAYOUT_H_
#define XIVRES_GAME_LAYOUT_H_

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace xivres {
	class pe_image;

	// What the game's code says about its structures: every signature of data/game_font_signatures.json matched once in
	// the .text section of the game's executable, and their captured offsets, sizes and counts merged by name, as
	// FontChanger.DalamudPlugin's GameLayout and CodeSignature do (that file's comment describes the format).
	//
	// A value is known when at least one signature captured it and none disagree, or it is an int constant of the file.
	// A part of a program resolves what it uses with resolve(), which throws with everything that isn't known.
	class game_layout {
	public:
		struct capture {
			uint64_t Address;
			std::optional<int32_t> Value;  // its bytes as a little-endian signed integer, if 1, 2 or 4 bytes
		};

		struct match {
			std::string Signature;
			uint64_t Address;  // what the signature resolves to; 0 if it resolves to nothing
			std::map<std::string, std::string> Types;  // of every capture it has, matched or not
			std::map<std::string, capture> Captures;  // of the groups that matched

			uint64_t target(const std::string& rel32) const;
		};

	private:
		std::map<std::string, match> m_matches;
		std::map<std::string, std::string> m_failures;
		std::map<std::string, std::pair<int32_t, std::string>> m_values;
		std::map<std::string, std::string> m_conflicts;
		std::map<std::string, std::string> m_strings;
		std::map<std::string, std::vector<int32_t>> m_lists;
		std::vector<std::string> m_problems;

	public:
		// json: the text of game_font_signatures.json; exe: the bytes of the game's executable file; imageBase: where the
		// game has it loaded, as the addresses are wanted (the file's preferred base to look at the file alone).
		game_layout(std::string_view json, std::span<const uint8_t> exe, uint64_t imageBase);

		// The same of the game's executable as an image, its file or loaded (pe_image::from_loaded, and its base for
		// imageBase); a loaded one has the code as it is now, with any hooks in it.
		game_layout(std::string_view json, const pe_image& image, uint64_t imageBase);

		// The signatures not found, and why (no match, or more than one); and the values that signatures disagree on.
		const std::map<std::string, std::string>& failures() const { return m_failures; }
		const std::map<std::string, std::string>& conflicts() const { return m_conflicts; }

		// Resolves what a part of a program uses (fn calls get() and the like); throws, with everything that isn't known,
		// if anything isn't.
		void resolve(std::string_view part, const std::function<void()>& fn);

		// A captured value or an int constant; records a problem and returns -1 if it isn't known.
		int32_t get(const std::string& name);

		// A captured value or an int constant, if known.
		std::optional<int32_t> try_get(const std::string& name) const;

		// A string constant, or a constant list of ints; records a problem and returns an empty one if there is none.
		std::string get_string(const std::string& name);
		std::vector<int32_t> get_list(const std::string& name);

		// What a signature resolves to, or with rel32, the target it captures; records a problem and returns 0 if the
		// signature wasn't found.
		uint64_t address(const std::string& signature, const std::string& rel32 = {});

		// The target of a rel32 that several signatures may capture, checking that they agree; records a problem and
		// returns 0 if none captured it.
		uint64_t target(const std::string& rel32);

		// A signature's match, or nullptr (with a problem recorded) if it wasn't found.
		const match* find(const std::string& signature);
	};
}

#endif
