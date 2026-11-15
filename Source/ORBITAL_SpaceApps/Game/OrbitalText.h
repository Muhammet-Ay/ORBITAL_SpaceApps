// ORBITAL text translation, plain C++ (no Unreal types), tested in Tools/StandaloneTests.
//
// The game's screens are written in English. A Translator turns a finished English line into
// another language just before it is drawn, so the screens need no per-language code:
//   1. exact match                       "Remove this part"
//   2. match with the numbers taken out  "Budget: 360 M credits" uses the entry "Budget: 1 M credits"
//   3. an ALL CAPS line uses the normal entry and is upper-cased for the language
//   4. otherwise the line is split at separators (" | ", three spaces, ": ", two spaces, ", ")
//      and each piece is translated on its own; a piece without an entry stays as it is.
// A line with no entry at all is returned unchanged, so a missing translation shows English.
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace Orbital
{

class Translator
{
public:
    void Add(const std::wstring& english, const std::wstring& translated)
    {
        exact_[english] = translated;
        lower_[Lower(english)] = translated;
        std::vector<std::wstring> numbers;
        const std::wstring pattern = Pattern(english, &numbers);
        if (!numbers.empty())
        {
            // Numbers in the translation point back at the numbers of the English entry, in order.
            std::wstring value;
            std::vector<bool> used(numbers.size(), false);
            for (size_t i = 0; i < translated.size();)
            {
                const size_t end = NumberEnd(translated, i);
                if (end == i)
                {
                    value += translated[i++];
                    continue;
                }
                const std::wstring number = translated.substr(i, end - i);
                size_t slot = numbers.size();
                for (size_t k = 0; k < numbers.size() && slot == numbers.size(); ++k)
                {
                    slot = !used[k] && numbers[k] == number ? k : slot;
                }
                if (slot < numbers.size())
                {
                    used[slot] = true;
                    value += Marker;
                    value += static_cast<wchar_t>(L'A' + slot);
                }
                else
                {
                    value += number;
                }
                i = end;
            }
            pattern_[pattern] = value;
            patternLower_[Lower(pattern)] = value;
        }
    }

    std::wstring Translate(const std::wstring& text, int depth = 0) const
    {
        size_t first = 0, last = text.size();
        while (first < last && text[first] == L' ') ++first;
        while (last > first && text[last - 1] == L' ') --last;
        if (first == last)
        {
            return text;
        }
        const std::wstring lead = text.substr(0, first), core = text.substr(first, last - first), trail = text.substr(last);
        std::wstring found;
        if (Lookup(core, found))
        {
            return lead + found + trail;
        }
        const wchar_t end = core.back();
        if ((end == L'.' || end == L':') && core.size() > 1 && Lookup(core.substr(0, core.size() - 1), found))
        {
            return lead + found + end + trail;
        }
        if (depth < 6)
        {
            static const wchar_t* const Separators[] = {L"  |  ", L"   ", L":  ", L": ", L"  ", L", "};
            for (const wchar_t* separator : Separators)
            {
                const size_t at = core.find(separator);
                if (at != std::wstring::npos && at > 0)
                {
                    const std::wstring gap(separator);
                    return lead + Translate(core.substr(0, at), depth + 1) + gap
                        + Translate(core.substr(at + gap.size()), depth + 1) + trail;
                }
            }
        }
        return text;
    }

    // Upper case with the Turkish dotted and dotless i.
    static std::wstring UpperTurkish(const std::wstring& text)
    {
        std::wstring out = text;
        for (wchar_t& c : out)
        {
            switch (c)
            {
            case L'i': c = L'İ'; break;
            case L'ı': c = L'I'; break;
            case L'ş': c = L'Ş'; break;
            case L'ğ': c = L'Ğ'; break;
            case L'ü': c = L'Ü'; break;
            case L'ö': c = L'Ö'; break;
            case L'ç': c = L'Ç'; break;
            case L'â': c = L'Â'; break;
            default: c = c >= L'a' && c <= L'z' ? static_cast<wchar_t>(c - L'a' + L'A') : c; break;
            }
        }
        return out;
    }

private:
    static constexpr wchar_t Marker = L'\x01';

    static bool IsDigit(wchar_t c) { return c >= L'0' && c <= L'9'; }

    // End of the number starting at i ("12", "0.03", "4,000"); i itself when there is none.
    static size_t NumberEnd(const std::wstring& s, size_t i)
    {
        size_t end = i;
        while (end < s.size() && IsDigit(s[end])) ++end;
        while (end > i && end + 1 < s.size() && (s[end] == L'.' || s[end] == L',') && IsDigit(s[end + 1]))
        {
            ++end;
            while (end < s.size() && IsDigit(s[end])) ++end;
        }
        return end;
    }

    static std::wstring Pattern(const std::wstring& s, std::vector<std::wstring>* numbers)
    {
        std::wstring out;
        for (size_t i = 0; i < s.size();)
        {
            const size_t end = NumberEnd(s, i);
            if (end == i)
            {
                out += s[i++];
                continue;
            }
            numbers->push_back(s.substr(i, end - i));
            out += L'#';
            i = end;
        }
        return out;
    }

    static std::wstring Lower(const std::wstring& s)
    {
        std::wstring out = s;
        for (wchar_t& c : out)
        {
            c = c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c - L'A' + L'a') : c;
        }
        return out;
    }

    static std::wstring Fill(const std::wstring& value, const std::vector<std::wstring>& numbers)
    {
        std::wstring out;
        for (size_t i = 0; i < value.size(); ++i)
        {
            if (value[i] == Marker && i + 1 < value.size())
            {
                const size_t slot = static_cast<size_t>(value[++i] - L'A');
                out += slot < numbers.size() ? numbers[slot] : std::wstring();
            }
            else
            {
                out += value[i];
            }
        }
        return out;
    }

    bool Lookup(const std::wstring& core, std::wstring& out) const
    {
        auto it = exact_.find(core);
        if (it != exact_.end())
        {
            out = it->second;
            return true;
        }
        std::vector<std::wstring> numbers;
        const std::wstring pattern = Pattern(core, &numbers);
        if (!numbers.empty() && (it = pattern_.find(pattern)) != pattern_.end())
        {
            out = Fill(it->second, numbers);
            return true;
        }
        // ALL CAPS: use the normal entry, then upper-case it.
        bool letters = false, caps = true;
        for (const wchar_t c : core)
        {
            letters = letters || (c >= L'A' && c <= L'Z');
            caps = caps && !(c >= L'a' && c <= L'z');
        }
        if (letters && caps)
        {
            if ((it = lower_.find(Lower(core))) != lower_.end())
            {
                out = UpperTurkish(it->second);
                return true;
            }
            if (!numbers.empty() && (it = patternLower_.find(Lower(pattern))) != patternLower_.end())
            {
                out = UpperTurkish(Fill(it->second, numbers));
                return true;
            }
        }
        return false;
    }

    std::unordered_map<std::wstring, std::wstring> exact_, lower_, pattern_, patternLower_;
};

// English -> Turkish for every line the game shows (generated: Tools/Localization/gen_turkish.py).
inline const Translator& TurkishText()
{
    static const Translator table = []
    {
        static const wchar_t* const Entries[][2] = {
#include "OrbitalTextTurkish.inl"
        };
        Translator t;
        for (const auto& entry : Entries)
        {
            t.Add(entry[0], entry[1]);
        }
        return t;
    }();
    return table;
}

} // namespace Orbital
