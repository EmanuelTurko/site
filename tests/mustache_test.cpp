#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>
#include <map>
#include <boost/json.hpp>
#include "site/mustache.hpp"

namespace fs = std::filesystem;
namespace json = boost::json;

// writes every {filename, template} into a fresh temp folder, renders `name`
auto renderFiles(const std::map<std::string, std::string>& files,
                 const std::string& name, const json::value& ctx) -> std::string {
    auto dir = fs::temp_directory_path() / "mustache_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    for (const auto& [file, tpl] : files)
        std::ofstream(dir / file) << tpl;
    mustache::TemplateSet ts(dir);
    return ts.render(name, ctx);
}

auto renderStr(const std::string& tpl, const json::value& ctx = json::object{}) -> std::string {
    return renderFiles({{"t.html", tpl}}, "t.html", ctx);
}

// ---------- variables ----------
TEST(Mustache, VarIsEscaped) {
    auto ctx = json::parse(R"({"name": "<b>"})");
    EXPECT_EQ(renderStr("hi {{name}}", ctx), "hi &lt;b&gt;");
}

TEST(Mustache, EscapesAllFiveChars) {
    auto ctx = json::parse(R"({"s": "& < > \" '"})");
    EXPECT_EQ(renderStr("{{s}}", ctx), "&amp; &lt; &gt; &quot; &#39;");
}

TEST(Mustache, TripleBracesAreRaw) {
    auto ctx = json::parse(R"({"h": "<i>x</i>"})");
    EXPECT_EQ(renderStr("{{{h}}}", ctx), "<i>x</i>");
}

TEST(Mustache, AmpersandIsRaw) {
    auto ctx = json::parse(R"({"h": "<i>x</i>"})");
    EXPECT_EQ(renderStr("{{& h}}", ctx), "<i>x</i>");
}

TEST(Mustache, MissingNameIsEmpty) {
    EXPECT_EQ(renderStr("[{{nope}}][{{{nope}}}]"), "[][]");
}

TEST(Mustache, DotPath) {
    auto ctx = json::parse(R"({"a": {"b": {"c": "deep"}}})");
    EXPECT_EQ(renderStr("{{a.b.c}}", ctx), "deep");
}

TEST(Mustache, MissingDotPathIsEmpty) {
    auto ctx = json::parse(R"({"a": {"b": 1}})");
    EXPECT_EQ(renderStr("[{{a.x}}][{{x.y}}]", ctx), "[][]");
}

TEST(Mustache, Numbers) {
    auto ctx = json::parse(R"({"i": 42, "d": 1.5})");
    EXPECT_EQ(renderStr("{{i}}/{{d}}", ctx), "42/1.5");
}

TEST(Mustache, CommentIsSkipped) {
    EXPECT_EQ(renderStr("a{{! ignore me }}b"), "ab");
}

// ---------- sections ----------
TEST(Mustache, SectionRepeatsOverArray) {
    auto ctx = json::parse(R"({"xs": [1, 2, 3]})");
    EXPECT_EQ(renderStr("{{#xs}}[{{.}}]{{/xs}}", ctx), "[1][2][3]");
}

TEST(Mustache, SectionOverArrayOfObjects) {
    auto ctx = json::parse(R"({"ps": [{"n": "a"}, {"n": "b"}]})");
    EXPECT_EQ(renderStr("{{#ps}}{{n}},{{/ps}}", ctx), "a,b,");
}

TEST(Mustache, SectionOnObjectOpensScope) {
    auto ctx = json::parse(R"({"o": {"x": "inner"}, "x": "outer"})");
    EXPECT_EQ(renderStr("{{#o}}{{x}}{{/o}}", ctx), "inner");
}

TEST(Mustache, InnerScopeFallsBackToOuter) {
    auto ctx = json::parse(R"({"o": {"y": 1}, "x": "outer"})");
    EXPECT_EQ(renderStr("{{#o}}{{x}}{{/o}}", ctx), "outer");
}

TEST(Mustache, TruthyNumberRendersOnceNoNewScope) {
    auto ctx = json::parse(R"({"n": 5, "t": "hi"})");
    EXPECT_EQ(renderStr("{{#n}}{{t}}{{/n}}", ctx), "hi");
}

TEST(Mustache, FalsyValuesHideSection) {
    auto ctx = json::parse(R"({"f": false, "e": [], "s": "", "z": null})");
    EXPECT_EQ(renderStr("[{{#f}}F{{/f}}{{#e}}E{{/e}}{{#s}}S{{/s}}{{#z}}Z{{/z}}{{#m}}M{{/m}}]", ctx), "[]");
}

TEST(Mustache, InvertedShowsForFalsy) {
    auto ctx = json::parse(R"({"f": false, "e": [], "t": true})");
    EXPECT_EQ(renderStr("{{^f}}F{{/f}}{{^e}}E{{/e}}{{^t}}T{{/t}}{{^m}}M{{/m}}", ctx), "FEM");
}

TEST(Mustache, NestedSections) {
    auto ctx = json::parse(R"({"ps": [{"n": "a", "tags": ["x", "y"]}, {"n": "b", "tags": []}]})");
    EXPECT_EQ(renderStr("{{#ps}}{{n}}:{{#tags}}{{.}}{{/tags}};{{/ps}}", ctx), "a:xy;b:;");
}

// ---------- partials ----------
TEST(Mustache, PartialUsesSameScope) {
    auto ctx = json::parse(R"({"ps": [{"n": "a"}, {"n": "b"}]})");
    auto out = renderFiles({{"main.html", "{{#ps}}{{> item.html}}{{/ps}}"},
                            {"item.html", "<{{n}}>"}},
                           "main.html", ctx);
    EXPECT_EQ(out, "<a><b>");
}

TEST(Mustache, UnknownPartialThrows) {
    EXPECT_THROW(renderStr("{{> nope.html}}"), std::exception);
}

// ---------- parse errors ----------
TEST(MustacheErrors, UnclosedSection) {
    EXPECT_THROW(renderStr("{{#a}}x"), mustache::ParseError);
}

TEST(MustacheErrors, MismatchedClose) {
    EXPECT_THROW(renderStr("{{#a}}x{{/b}}"), mustache::ParseError);
}

TEST(MustacheErrors, CloseWithoutOpen) {
    EXPECT_THROW(renderStr("x{{/a}}"), mustache::ParseError);
}

TEST(MustacheErrors, UnclosedTag) {
    EXPECT_THROW(renderStr("hi {{name"), mustache::ParseError);
}

TEST(MustacheErrors, EmptyTag) {
    EXPECT_THROW(renderStr("{{ }}"), mustache::ParseError);
}

TEST(MustacheErrors, UnclosedSectionReportsOpeningLine) {
    try {
        renderStr("{{#a}}\n{{b}}");
        FAIL() << "expected ParseError";
    } catch (const mustache::ParseError& e) {
        EXPECT_NE(std::string(e.what()).find("line 1"), std::string::npos) << e.what();
    }
}