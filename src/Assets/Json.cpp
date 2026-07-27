//=============================================================================
// Json.cpp
//   Json の実装。再帰下降パーサ。
//=============================================================================
#include "Json.h"

#include <cstdlib>
#include <stdexcept>

namespace dx12::assets::json
{
namespace
{
/// <summary>読み取り位置を持ちながら 1 文字ずつ進めるための状態。</summary>
struct Reader
{
    /// <summary>解析対象のテキスト。</summary>
    const std::string& text;

    /// <summary>次に読む位置。</summary>
    size_t position = 0;

    /// <summary>終端まで来たかどうか。</summary>
    bool AtEnd() const noexcept { return position >= text.size(); }

    /// <summary>現在の文字を覗きます（進めません）。</summary>
    char Peek() const { return AtEnd() ? '\0' : text[position]; }

    /// <summary>1 文字読んで進めます。</summary>
    char Take() { return AtEnd() ? '\0' : text[position++]; }

    /// <summary>空白・改行を読み飛ばします。</summary>
    void SkipWhitespace()
    {
        while (!AtEnd())
        {
            const char c = text[position];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            {
                ++position;
            }
            else
            {
                break;
            }
        }
    }

    /// <summary>期待した文字が来ているか確かめて読み進めます。</summary>
    /// <exception cref="std::runtime_error">違う文字だった場合。</exception>
    void Expect(char expected)
    {
        if (Take() != expected)
        {
            throw std::runtime_error("JSON の構文が壊れています。");
        }
    }
};

Value ParseValue(Reader& reader);


/// <summary>
/// UTF-16 のコードポイントを UTF-8 の並びとして追加します。
/// </summary>
/// <param name="codePoint">追加する文字。</param>
/// <param name="output">追加先の文字列。</param>
void AppendUtf8(uint32_t codePoint, std::string& output)
{
    if (codePoint < 0x80)
    {
        output.push_back(static_cast<char>(codePoint));
    }
    else if (codePoint < 0x800)
    {
        output.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
    else if (codePoint < 0x10000)
    {
        output.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
        output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
    else
    {
        output.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
        output.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
}


/// <summary>4 桁の 16 進数を読み取ります。</summary>
uint32_t ParseHex4(Reader& reader)
{
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i)
    {
        const char c = reader.Take();
        value <<= 4;

        if (c >= '0' && c <= '9')      { value |= static_cast<uint32_t>(c - '0'); }
        else if (c >= 'a' && c <= 'f') { value |= static_cast<uint32_t>(c - 'a' + 10); }
        else if (c >= 'A' && c <= 'F') { value |= static_cast<uint32_t>(c - 'A' + 10); }
        else { throw std::runtime_error("JSON の \\u エスケープが壊れています。"); }
    }
    return value;
}


/// <summary>ダブルクォートで囲まれた文字列を読み取ります。</summary>
std::string ParseString(Reader& reader)
{
    reader.Expect('"');

    std::string result;
    while (true)
    {
        if (reader.AtEnd())
        {
            throw std::runtime_error("JSON の文字列が閉じられていません。");
        }

        const char c = reader.Take();
        if (c == '"')
        {
            break;
        }

        if (c != '\\')
        {
            result.push_back(c);
            continue;
        }

        // エスケープ
        const char escape = reader.Take();
        switch (escape)
        {
        case '"':  result.push_back('"');  break;
        case '\\': result.push_back('\\'); break;
        case '/':  result.push_back('/');  break;
        case 'b':  result.push_back('\b'); break;
        case 'f':  result.push_back('\f'); break;
        case 'n':  result.push_back('\n'); break;
        case 'r':  result.push_back('\r'); break;
        case 't':  result.push_back('\t'); break;
        case 'u':
        {
            uint32_t codePoint = ParseHex4(reader);

            // サロゲートペア。UTF-16 で 1 文字を 2 つに分けて書いたもの。
            if (codePoint >= 0xD800 && codePoint <= 0xDBFF
                && reader.Peek() == '\\')
            {
                const size_t saved = reader.position;
                reader.Take();                      // '\'
                if (reader.Take() == 'u')
                {
                    const uint32_t low = ParseHex4(reader);
                    if (low >= 0xDC00 && low <= 0xDFFF)
                    {
                        codePoint = 0x10000
                                  + ((codePoint - 0xD800) << 10)
                                  + (low - 0xDC00);
                    }
                    else
                    {
                        reader.position = saved;
                    }
                }
                else
                {
                    reader.position = saved;
                }
            }

            AppendUtf8(codePoint, result);
            break;
        }
        default:
            throw std::runtime_error("JSON に未知のエスケープがあります。");
        }
    }

    return result;
}


/// <summary>数値を読み取ります。</summary>
double ParseNumber(Reader& reader)
{
    const char* start = reader.text.c_str() + reader.position;
    char* end = nullptr;

    const double value = std::strtod(start, &end);
    if (end == start)
    {
        throw std::runtime_error("JSON の数値が壊れています。");
    }

    reader.position += static_cast<size_t>(end - start);
    return value;
}


/// <summary>`true` / `false` / `null` のいずれかを読み取ります。</summary>
Value ParseLiteral(Reader& reader)
{
    const auto matches = [&reader](const char* word) {
        const size_t length = std::char_traits<char>::length(word);
        return reader.text.compare(reader.position, length, word) == 0;
    };

    Value value;

    if (matches("true"))
    {
        value.type = Value::Type::Boolean;
        value.boolean = true;
        reader.position += 4;
    }
    else if (matches("false"))
    {
        value.type = Value::Type::Boolean;
        value.boolean = false;
        reader.position += 5;
    }
    else if (matches("null"))
    {
        value.type = Value::Type::Null;
        reader.position += 4;
    }
    else
    {
        throw std::runtime_error("JSON に解釈できない字句があります。");
    }

    return value;
}


/// <summary>配列を読み取ります。</summary>
Value ParseArray(Reader& reader)
{
    reader.Expect('[');

    Value value;
    value.type = Value::Type::Array;

    reader.SkipWhitespace();
    if (reader.Peek() == ']')
    {
        reader.Take();
        return value;
    }

    while (true)
    {
        value.elements.push_back(ParseValue(reader));

        reader.SkipWhitespace();
        const char c = reader.Take();

        if (c == ']') { break; }
        if (c != ',') { throw std::runtime_error("JSON の配列が壊れています。"); }
    }

    return value;
}


/// <summary>オブジェクトを読み取ります。</summary>
Value ParseObject(Reader& reader)
{
    reader.Expect('{');

    Value value;
    value.type = Value::Type::Object;

    reader.SkipWhitespace();
    if (reader.Peek() == '}')
    {
        reader.Take();
        return value;
    }

    while (true)
    {
        reader.SkipWhitespace();
        std::string name = ParseString(reader);

        reader.SkipWhitespace();
        reader.Expect(':');

        value.members.emplace_back(std::move(name), ParseValue(reader));

        reader.SkipWhitespace();
        const char c = reader.Take();

        if (c == '}') { break; }
        if (c != ',') { throw std::runtime_error("JSON のオブジェクトが壊れています。"); }
    }

    return value;
}


/// <summary>先頭の 1 文字を見て、どの種類かを振り分けます。</summary>
Value ParseValue(Reader& reader)
{
    reader.SkipWhitespace();

    switch (reader.Peek())
    {
    case '{': return ParseObject(reader);
    case '[': return ParseArray(reader);
    case '"':
    {
        Value value;
        value.type = Value::Type::String;
        value.text = ParseString(reader);
        return value;
    }
    case 't':
    case 'f':
    case 'n':
        return ParseLiteral(reader);
    default:
    {
        Value value;
        value.type = Value::Type::Number;
        value.number = ParseNumber(reader);
        return value;
    }
    }
}

/// <summary>`Member` などが「見つからなかった」ときに返す共有の値。</summary>
const Value kNullValue;
} // namespace


/// <summary>
/// オブジェクトから名前で値を探します。
/// </summary>
const Value* Value::Member(const char* name) const
{
    if (type != Type::Object)
    {
        return nullptr;
    }

    for (const auto& member : members)
    {
        if (member.first == name)
        {
            return &member.second;
        }
    }

    return nullptr;
}


/// <summary>
/// 配列の要素数、またはオブジェクトのメンバ数を返します。
/// </summary>
size_t Value::Size() const noexcept
{
    if (type == Type::Array)  { return elements.size(); }
    if (type == Type::Object) { return members.size(); }
    return 0;
}


/// <summary>
/// 配列の要素を取り出します。
/// </summary>
const Value& Value::At(size_t index) const
{
    if (type != Type::Array || index >= elements.size())
    {
        return kNullValue;
    }
    return elements[index];
}


/// <summary>
/// 数値として取り出します。
/// </summary>
double Value::AsNumber(double fallback) const noexcept
{
    return (type == Type::Number) ? number : fallback;
}


/// <summary>
/// 整数として取り出します。
/// </summary>
int Value::AsInt(int fallback) const noexcept
{
    return (type == Type::Number) ? static_cast<int>(number) : fallback;
}


/// <summary>
/// 文字列として取り出します。
/// </summary>
const std::string& Value::AsString() const
{
    return (type == Type::String) ? text : kNullValue.text;
}


/// <summary>
/// JSON テキストを解析します。
/// </summary>
Value Parse(const std::string& text)
{
    Reader reader{ text, 0 };

    Value value = ParseValue(reader);

    reader.SkipWhitespace();
    if (!reader.AtEnd())
    {
        throw std::runtime_error("JSON の末尾に余分なデータがあります。");
    }

    return value;
}

} // namespace dx12::assets::json
