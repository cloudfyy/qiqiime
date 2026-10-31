#pragma once
#include <array>
#include <cstdint>
#include <string_view>
#include <utility>
#include <gsl/gsl>

#include "Ime/engine/binary_dict.h"

namespace qiqi {

struct WubiEntry {
    const wchar_t* code;
    const wchar_t* word;
};

struct PackedBuiltinWubiEntry {
    uint32_t packed_code = 0;
    std::wstring_view word;
};

// Built-in core Wubi 86 dictionary (一级简码, 高频二级/三级/四级简码及常用词组)
// External data/wubi86.txt can extend or override these entries at runtime.
inline constexpr WubiEntry kBuiltinWubi86Dict[] = {
    // 25 一级简码 (a-y)
    {L"a", L"工"}, {L"b", L"了"}, {L"c", L"以"}, {L"d", L"在"}, {L"e", L"有"},
    {L"f", L"地"}, {L"g", L"一"}, {L"h", L"上"}, {L"i", L"不"}, {L"j", L"是"},
    {L"k", L"中"}, {L"l", L"国"}, {L"m", L"同"}, {L"n", L"民"}, {L"o", L"为"},
    {L"p", L"这"}, {L"q", L"我"}, {L"r", L"的"}, {L"s", L"要"}, {L"t", L"和"},
    {L"u", L"产"}, {L"v", L"发"}, {L"w", L"人"}, {L"x", L"经"}, {L"y", L"主"},

    // 高频单字与简码
    {L"aa", L"式"}, {L"aad", L"匿"}, {L"aah", L"芽"}, {L"aak", L"戒"},
    {L"ad", L"基"}, {L"af", L"革"}, {L"ag", L"七"}, {L"ah", L"牙"}, {L"aj", L"划"},
    {L"ak", L"或"}, {L"al", L"东"}, {L"am", L"黄"}, {L"an", L"节"}, {L"aq", L"匹"},
    {L"ar", L"匠"}, {L"as", L"某"}, {L"at", L"世"}, {L"au", L"共"}, {L"av", L"切"},
    {L"aw", L"区"}, {L"ax", L"药"}, {L"ay", L"芳"},

    {L"ba", L"子"}, {L"bb", L"卫"}, {L"bc", L"取"}, {L"bd", L"承"}, {L"be", L"乃"},
    {L"bf", L"耻"}, {L"bg", L"阳"}, {L"bh", L"职"}, {L"bi", L"孙"}, {L"bj", L"阶"},
    {L"bk", L"出"}, {L"bl", L"阵"}, {L"bm", L"邮"}, {L"bn", L"收"}, {L"bo", L"队"},
    {L"bp", L"辽"}, {L"bq", L"聊"}, {L"br", L"际"}, {L"bs", L"联"}, {L"bt", L"降"},

    {L"ca", L"戏"}, {L"cb", L"邓"}, {L"cc", L"双"}, {L"cd", L"参"}, {L"ce", L"劝"},
    {L"cf", L"圣"}, {L"cg", L"马"}, {L"ch", L"观"}, {L"ci", L"能"}, {L"cj", L"难"},
    {L"ck", L"台"}, {L"cl", L"欢"}, {L"cm", L"仅"}, {L"cn", L"允"}, {L"co", L"欢"},

    {L"da", L"功"}, {L"db", L"顾"}, {L"dc", L"友"}, {L"dd", L"大"}, {L"de", L"有"},
    {L"df", L"压"}, {L"dg", L"三"}, {L"dh", L"左"}, {L"di", L"原"}, {L"dj", L"古"},
    {L"dk", L"右"}, {L"dl", L"面"}, {L"dm", L"页"}, {L"dn", L"成"}, {L"do", L"灰"},

    {L"gg", L"五"}, {L"gggg", L"王"}, {L"ggh", L"正"}, {L"ggll", L"五笔"},
    {L"tt", L"禾"}, {L"ttn", L"气"}, {L"ttgf", L"笔"},
    {L"k", L"中"}, {L"kh", L"中"}, {L"khl", L"中"},
    {L"l", L"国"}, {L"lg", L"国"}, {L"lgy", L"国"}, {L"lgyi", L"国"},
    {L"kl", L"中国"}, {L"khl", L"中"},
    {L"wq", L"你"}, {L"wqi", L"你"}, {L"wqiy", L"你"},
    {L"vb", L"好"}, {L"vbg", L"好"}, {L"wqvb", L"你好"},
    {L"ww", L"从"}, {L"www", L"众"}, {L"wwww", L"工"},
    {L"ss", L"林"}, {L"sss", L"森"}, {L"ssss", L"木"},
    {L"oo", L"炎"}, {L"ooo", L"焱"}, {L"oooo", L"火"},
    {L"ii", L"吕"}, {L"iii", L"品"}, {L"iiii", L"水"},
    {L"ff", L"圭"}, {L"fff", L"垚"}, {L"ffff", L"土"},
    {L"qq", L"爻"}, {L"qqq", L"鑫"}, {L"qqqq", L"金"},
    {L"yn", L"门"}, {L"yng", L"闰"},
    {L"lwg", L"输入"}, {L"lwgi", L"输入法"}, {L"ic", L"法"}, {L"iag", L"法"},
    {L"yb", L"方"}, {L"ybg", L"方"}, {L"ud", L"立"}, {L"uj", L"音"},
    {L"v", L"发"}, {L"vg", L"发"}, {L"vna", L"开发"}, {L"gde", L"开发"},
    {L"ip", L"学"}, {L"ipb", L"学"}, {L"ipvf", L"学习"},
    {L"jg", L"日"}, {L"jgj", L"晶"}, {L"jjj", L"晶"}, {L"jjjj", L"日"}
};

consteval auto BuildSortedBuiltinWubi86Dict() noexcept {
    constexpr size_t kCount = std::size(kBuiltinWubi86Dict);
    std::array<PackedBuiltinWubiEntry, kCount> arr{};
    for (size_t i = 0; i < kCount; ++i) {
        gsl::at(arr, i) = PackedBuiltinWubiEntry{
            PackWubiCode(gsl::at(kBuiltinWubi86Dict, i).code),
            gsl::at(kBuiltinWubi86Dict, i).word
        };
    }
    for (size_t i = 1; i < kCount; ++i) {
        const PackedBuiltinWubiEntry key = gsl::at(arr, i);
        size_t j = i;
        while (j > 0 && gsl::at(arr, j - 1).packed_code > key.packed_code) {
            gsl::at(arr, j) = gsl::at(arr, j - 1);
            --j;
        }
        gsl::at(arr, j) = key;
    }
    return arr;
}

// Compile-time sorted builtin Wubi 86 table residing in read-only .rdata (zero runtime heap allocation).
inline constexpr auto kSortedBuiltinWubi86Dict = BuildSortedBuiltinWubi86Dict();

} // namespace qiqi
