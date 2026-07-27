//=============================================================================
// Json.h
//   glTF を読むための最小限の JSON パーサ。
//
//   外部ライブラリを使わない方針なので自前で持つ。
//   仕様（RFC 8259）のうち、glTF に必要な範囲だけを実装している。
//=============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dx12::assets::json
{

/// <summary>
/// JSON の値 1 つを表すクラス。
/// </summary>
/// <remarks>
/// 型ごとにクラスを分けず、1 つの構造体に全ての入れ物を持たせています。
/// メモリは無駄になりますが、読み手が追いやすさを優先しました。
/// </remarks>
class Value
{
public:
    /// <summary>JSON の値の種類。</summary>
    enum class Type
    {
        /// <summary>`null`。値が無いことも表します。</summary>
        Null,

        /// <summary>`true` / `false`。</summary>
        Boolean,

        /// <summary>数値。JSON は整数と小数を区別しないため、常に `double` です。</summary>
        Number,

        /// <summary>文字列。</summary>
        String,

        /// <summary>配列。</summary>
        Array,

        /// <summary>オブジェクト（名前と値の組の並び）。</summary>
        Object,
    };

    /// <summary>この値の種類。</summary>
    Type type = Type::Null;

    /// <summary>`Boolean` のときの中身。</summary>
    bool boolean = false;

    /// <summary>`Number` のときの中身。</summary>
    double number = 0.0;

    /// <summary>`String` のときの中身（UTF-8）。</summary>
    std::string text;

    /// <summary>`Array` のときの要素。</summary>
    std::vector<Value> elements;

    /// <summary>`Object` のときの、名前と値の組。</summary>
    /// <remarks>
    /// ハッシュ表ではなく素直な配列です。glTF のオブジェクトは要素数が少なく、
    /// 順番に探しても速度は問題になりません。
    /// </remarks>
    std::vector<std::pair<std::string, Value>> members;

    /// <summary>
    /// オブジェクトから名前で値を探します。
    /// </summary>
    /// <param name="name">探す名前。</param>
    /// <returns>見つかればその値へのポインタ。無ければ `nullptr`。</returns>
    const Value* Member(const char* name) const;

    /// <summary>
    /// 配列の要素数、またはオブジェクトのメンバ数を返します。
    /// </summary>
    /// <returns>要素数。それ以外の型なら 0。</returns>
    size_t Size() const noexcept;

    /// <summary>
    /// 配列の要素を取り出します。
    /// </summary>
    /// <param name="index">要素の番号。</param>
    /// <returns>要素。範囲外なら `Null` の値。</returns>
    const Value& At(size_t index) const;

    /// <summary>
    /// 数値として取り出します。
    /// </summary>
    /// <param name="fallback">数値でなかった場合に返す値。</param>
    /// <returns>数値、または `fallback`。</returns>
    double AsNumber(double fallback = 0.0) const noexcept;

    /// <summary>
    /// 整数として取り出します。
    /// </summary>
    /// <param name="fallback">数値でなかった場合に返す値。</param>
    /// <returns>整数、または `fallback`。</returns>
    int AsInt(int fallback = -1) const noexcept;

    /// <summary>
    /// 文字列として取り出します。
    /// </summary>
    /// <returns>文字列。文字列でなければ空文字。</returns>
    const std::string& AsString() const;
};


/// <summary>
/// JSON テキストを解析します。
/// </summary>
/// <param name="text">解析する UTF-8 のテキスト。</param>
/// <returns>いちばん外側の値。</returns>
/// <exception cref="std::runtime_error">構文が壊れている場合。</exception>
Value Parse(const std::string& text);

} // namespace dx12::assets::json
