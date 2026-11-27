#ifdef _WIN32

#include <algorithm>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "fs/harness/dir.h"
#include "fs/harness/files.h"
#include "fs/harness/win32.h"
#include "kotatsu/zest/zest.h"
#include "kotatsu/fs/directory.h"
#include "kotatsu/fs/file.h"
#include "kotatsu/fs/path.h"
#include "kotatsu/fs/resolve.h"
#include "kotatsu/fs/status.h"
#include "kotatsu/fs/volume.h"

// WSL symlinks, which WSL and Cygwin's `ln -s` make and Win32 cannot open,
// followed where kotatsu opens a path as WSL and Cygwin follow them.

namespace kotatsu::fs {

namespace {

/// A directory of a case's own holding `real/h.h` and `real/sub/s.h`, for
/// the links the case makes to them.
struct Tree {
    Tree() {
        std::filesystem::create_directories(dir.path / "real" / "sub");
        test::write_text(dir.file("real/h.h"), "h");
        test::write_text(dir.file("real/sub/s.h"), "s");
    }

    /// `name` in the directory, as kotatsu takes it.
    std::string at(std::string_view name) const {
        return dir.file(name);
    }

    /// Makes `l1` a link to `l2`, and so on to `l<count>`, a link to
    /// `real/h.h`.
    void make_chain(int count) const {
        for(int i = 1; i < count; ++i) {
            test::make_wsl_symlink(at(std::format("l{}", i)), std::format("l{}", i + 1));
        }
        test::make_wsl_symlink(at(std::format("l{}", count)), "real/h.h");
    }

    /// canonical() of `name` in the directory, which the case checks a
    /// link's against.
    std::string real(std::string_view name) const {
        ZEST_CONTEXT("{}", name);
        std::string resolved;
        ZASSERT(!canonical(resolved, at(name)));
        return resolved;
    }

    test::TempDir dir;
};

/// What read_file() reads of `path`, or why it failed.
std::string text_at(std::string_view path) {
    std::string text;
    if(auto error = read_file(text, path)) {
        return "error: " + error.message();
    }
    return text;
}

/// The error status() of `path` fails with.
std::error_code status_error(std::string_view path) {
    ZEST_CONTEXT("{}", path);
    auto found = status(path);
    ZASSERT(!found);
    return found.error();
}

ZEST_SUITE(fs_windows_wsl_symlink, Tree) {

ZEST_CASE(a_relative_target_is_taken_from_the_link_directory) {
    test::make_wsl_symlink(at("rel.h"), "real/h.h");
    test::make_wsl_symlink(at("real/sub/up.h"), "../h.h");
    ZEXPECT(text_at(at("rel.h")) == "h");
    ZEXPECT(text_at(at("real/sub/up.h")) == "h");
    // The target in UTF-8, as WSL keeps it.
    test::write_text(at("é中.h"), "é");
    test::make_wsl_symlink(at("unicode.h"), "é中.h");
    ZEXPECT(text_at(at("unicode.h")) == "é");
    auto followed = status(at("rel.h"));
    ZASSERT(followed.has_value());
    ZEXPECT(followed->type == FileType::Regular);
    ZEXPECT(followed->size == 1u);
    // The link itself is as it was.
    auto itself = symlink_status(at("rel.h"));
    ZASSERT(itself.has_value());
    ZEXPECT(itself->type == FileType::Symlink);
    std::string target;
    ZEXPECT(!read_symlink(target, at("rel.h")));
    ZEXPECT(target == "real/h.h");
}

ZEST_CASE(a_target_under_mnt_is_on_that_drive) {
    // WSL reaches drive C at /mnt/c, and Cygwin writes its /cygdrive/c so.
    auto mounted = test::wsl_path(dir.root());
    if(mounted.empty()) {
        // The temp directory is on a share or a volume without a letter.
        zest::skip();
        return;
    }
    test::make_wsl_symlink(at("abs.h"), mounted + "/real/h.h");
    test::make_wsl_symlink(at("absdir"), mounted + "/real");
    ZEXPECT(text_at(at("abs.h")) == "h");
    ZEXPECT(text_at(at("absdir/sub/s.h")) == "s");
    std::string resolved;
    ZEXPECT(!canonical(resolved, at("abs.h")));
    ZEXPECT(resolved == real("real/h.h"));
    // The drive's letter in either case, which Windows does not tell apart.
    auto upper = mounted;
    upper[5] = static_cast<char>(upper[5] - 'a' + 'A');
    test::make_wsl_symlink(at("upper.h"), upper + "/real/h.h");
    ZEXPECT(text_at(at("upper.h")) == "h");
    // The drive itself, and a `..` that climbs out of it into /mnt.
    test::make_wsl_symlink(at("drive"), mounted.substr(0, 6));
    ZEXPECT(text_at(at("drive") + mounted.substr(6) + "/real/h.h") == "h");
    test::make_wsl_symlink(at("around.h"),
                           mounted.substr(0, 6) + "/.." + mounted.substr(4) + "/real/h.h");
    ZEXPECT(text_at(at("around.h")) == "h");
}

ZEST_CASE(a_relative_target_climbs_from_the_drive_into_mnt) {
    // As WSL climbs from /mnt/c, the drive's root, into /mnt, where a
    // Windows path would stay at the root.
    auto mounted = test::wsl_path(dir.root());
    if(mounted.empty()) {
        // The temp directory is on a share or a volume without a letter.
        zest::skip();
        return;
    }
    auto root = dir.root();
    std::string climb;
    for([[maybe_unused]] auto name: path::components(root)) {
        climb += "../";
    }
    test::make_wsl_symlink(at("climb.h"), climb + "../" + mounted.substr(5) + "/real/h.h");
    ZEXPECT(text_at(at("climb.h")) == "h");
    // So does one after names of the target, climbed as they are reached:
    // out of `real` and each directory above it, then out of the drive.
    std::string up = mounted + "/real";
    for([[maybe_unused]] auto name: path::components(root)) {
        up += "/..";
    }
    up += "/../..";
    test::make_wsl_symlink(at("again.h"), up + mounted.substr(4) + "/real/h.h");
    ZEXPECT(text_at(at("again.h")) == "h");
    test::make_wsl_symlink(at("above.h"), up + "/usr/h.h");
    ZEXPECT(status_error(at("above.h")) == std::errc::not_supported);
}

ZEST_CASE(a_directory_on_the_way_is_followed) {
    test::make_wsl_symlink(at("dirlink"), "real");
    ZEXPECT(text_at(at("dirlink/sub/s.h")) == "s");
    auto followed = status(at("dirlink/sub"));
    ZASSERT(followed.has_value());
    ZEXPECT(followed->type == FileType::Directory);
    // A call that leaves the last name itself goes through the directory.
    auto itself = symlink_status(at("dirlink/sub/s.h"));
    ZASSERT(itself.has_value());
    ZEXPECT(itself->type == FileType::Regular);
    test::make_wsl_symlink(at("real/sub/up.h"), "../h.h");
    std::string target;
    ZEXPECT(!read_symlink(target, at("dirlink/sub/up.h")));
    ZEXPECT(target == "../h.h");
    ZEXPECT(text_at(at("dirlink/sub/up.h")) == "h");
}

ZEST_CASE(a_target_climbs_out_of_where_the_directory_is) {
    // A `..` climbs out of where a junction before the link leads, as Cygwin
    // and POSIX climb out of where a symlink leads; Windows' own relative
    // links climb back out of the junction, by text.
    test::make_junction(at("junction"), at(R"(real\sub)"));
    test::make_wsl_symlink(at("real/sub/up.h"), "../h.h");
    test::write_text(at("h.h"), "top");
    ZEXPECT(text_at(at("junction/up.h")) == "h");
}

ZEST_CASE(a_target_climbs_out_of_where_a_link_in_it_leads) {
    // As Cygwin and POSIX climb, not back to the name the target spells.
    test::make_wsl_symlink(at("inner"), "real/sub");
    test::make_wsl_symlink(at("outer.h"), "inner/../h.h");
    test::make_junction(at("junction"), at(R"(real\sub)"));
    test::make_wsl_symlink(at("through.h"), "junction/../h.h");
    test::write_text(at("h.h"), "top");
    ZEXPECT(text_at(at("outer.h")) == "h");
    ZEXPECT(text_at(at("through.h")) == "h");
}

ZEST_CASE(a_target_with_a_separator_at_the_end_asks_for_a_directory) {
    test::make_wsl_symlink(at("slash.h"), "real/h.h/");
    test::make_wsl_symlink(at("dot.h"), "real/h.h/.");
    test::make_wsl_symlink(at("dir"), "real/sub/");
    ZEXPECT(status_error(at("slash.h")) == std::errc::not_a_directory);
    ZEXPECT(status_error(at("dot.h")) == std::errc::not_a_directory);
    auto file = File::open(at("slash.h"));
    ZASSERT(!file);
    ZEXPECT(file.error() == std::errc::not_a_directory);
    auto followed = status(at("dir"));
    ZASSERT(followed.has_value());
    ZEXPECT(followed->type == FileType::Directory);
}

ZEST_CASE(a_stream_through_a_link_is_the_stream) {
    // `a:stream` after the link names a stream of `a`, no drive.
    test::write_text(at("real/a:stream"), "stream");
    test::make_wsl_symlink(at("dirlink"), "real");
    ZEXPECT(text_at(at("dirlink/a:stream")) == "stream");
}

ZEST_CASE(a_link_to_a_link_is_followed) {
    test::make_wsl_symlink(at("a"), "b");
    test::make_wsl_symlink(at("b"), "dirlink/h.h");
    test::make_wsl_symlink(at("dirlink"), "real");
    ZEXPECT(text_at(at("a")) == "h");
    std::string resolved;
    ZEXPECT(!canonical(resolved, at("a")));
    ZEXPECT(resolved == real("real/h.h"));
}

ZEST_CASE(forty_links_are_followed) {
    // As many as POSIX follows in a path.
    make_chain(40);
    ZEXPECT(text_at(at("l1")) == "h");
}

ZEST_CASE(a_forty_first_link_fails) {
    make_chain(41);
    ZEXPECT(status_error(at("l1")) == std::errc::too_many_symbolic_link_levels);
}

ZEST_CASE(a_loop_fails) {
    test::make_wsl_symlink(at("self"), "self");
    test::make_wsl_symlink(at("ping"), "pong");
    test::make_wsl_symlink(at("pong"), "./ping");
    for(auto name: {"self", "ping", "ping/x"}) {
        ZEST_CONTEXT("{}", name);
        ZEXPECT(status_error(at(name)) == std::errc::too_many_symbolic_link_levels);
        auto file = File::open(at(name));
        ZASSERT(!file);
        ZEXPECT(file.error() == std::errc::too_many_symbolic_link_levels);
        std::string resolved;
        ZEXPECT(canonical(resolved, at(name)) == std::errc::too_many_symbolic_link_levels);
    }
}

ZEST_CASE(a_posix_target_fails) {
    // Only Cygwin's or WSL's own mount table resolves one: not_supported,
    // which the message says, where no_such_file_or_directory would let a
    // search take another file of the name.
    test::make_wsl_symlink(at("usr.h"), "/usr/include/stdio.h");
    test::make_wsl_symlink(at("usr"), "/usr/include");
    test::make_wsl_symlink(at("wsl"), "/mnt/wsl");
    test::make_wsl_symlink(at("mnt"), "/mnt");
    test::make_wsl_symlink(at("named"), "/mnt/cd/x");
    test::make_wsl_symlink(at("mounts"), "/mnt/");
    test::make_wsl_symlink(at("digit"), "/mnt/1/x");
    test::make_wsl_symlink(at("above"), "/mnt/c/../../usr");
    for(auto name: {"usr.h", "usr/stdio.h", "wsl", "mnt", "named", "mounts", "digit", "above"}) {
        ZEST_CONTEXT("{}", name);
        auto error = status_error(at(name));
        ZEXPECT(error == std::errc::not_supported);
        ZEXPECT(zest::contains(error.message(), "WSL symlink to a POSIX path"));
        auto file = File::open(at(name));
        ZASSERT(!file);
        ZEXPECT(file.error() == std::errc::not_supported);
        ZEXPECT(try_exists(at(name)).error_or({}) == std::errc::not_supported);
    }
    // The link itself, and what it holds, as before.
    auto itself = symlink_status(at("usr.h"));
    ZASSERT(itself.has_value());
    ZEXPECT(itself->type == FileType::Symlink);
    std::string target;
    ZEXPECT(!read_symlink(target, at("usr.h")));
    ZEXPECT(target == "/usr/include/stdio.h");
}

ZEST_CASE(a_dangling_link_names_nothing) {
    test::make_wsl_symlink(at("dangling"), "missing.h");
    ZEXPECT(status_error(at("dangling")) == std::errc::no_such_file_or_directory);
    ZEXPECT(try_exists(at("dangling")) == false);
    auto itself = symlink_status(at("dangling"));
    ZASSERT(itself.has_value());
    ZEXPECT(itself->type == FileType::Symlink);
}

ZEST_CASE(a_link_to_a_file_no_open_reaches_tells_its_status) {
    // A reparse point that holds the file, which no open reaches either: a
    // fifo WSL made.
    test::write_text(at("fifo"), "");
    test::set_reparse_point(at("fifo"), test::wsl_fifo_tag);
    test::make_wsl_symlink(at("to_fifo"), "fifo");
    auto followed = status(at("to_fifo"));
    ZASSERT(followed.has_value());
    ZEXPECT(followed->type == FileType::Fifo);
    // An open fails as it does without the link.
    auto file = File::open(at("to_fifo"));
    ZASSERT(!file);
    ZEXPECT(file.error() == std::errc::permission_denied);
    auto direct = File::open(at("fifo"));
    ZASSERT(!direct);
    ZEXPECT(direct.error() == std::errc::permission_denied);
}

ZEST_CASE(a_separator_after_a_link_asks_for_a_directory) {
    // Which Windows asks before it meets the link, a file.
    test::make_wsl_symlink(at("dirlink"), "real");
    test::make_wsl_symlink(at("rel.h"), "real/h.h");
    auto followed = status(at("dirlink") + "\\");
    ZASSERT(followed.has_value());
    ZEXPECT(followed->type == FileType::Directory);
    ZEXPECT(Directory::open(at("dirlink") + "\\").has_value());
    ZEXPECT(status_error(at("rel.h") + "\\") == std::errc::not_a_directory);
}

ZEST_CASE(a_link_without_a_target_fails) {
    // One of the tag that holds less than the format's version, and one
    // that holds an empty target, which WSL never makes.
    test::write_text(at("short"), "");
    test::set_reparse_point(at("short"), test::wsl_symlink_tag);
    test::make_wsl_symlink(at("empty"), "");
    for(auto name: {"short", "empty"}) {
        ZEST_CONTEXT("{}", name);
        ZEXPECT(status_error(at(name)) ==
                std::error_code(ERROR_INVALID_REPARSE_DATA, std::system_category()));
        std::string target;
        ZEXPECT(read_symlink(target, at(name)) == std::errc::invalid_argument);
    }
}

ZEST_CASE(a_target_windows_cannot_spell_names_nothing) {
    // A NUL, which no name holds, and bytes that are no UTF-8.
    test::make_wsl_symlink(at("nul.h"), std::string_view("a\0b", 3));
    test::make_wsl_symlink(at("bytes.h"), "\xFF.h");
    ZEXPECT(status_error(at("nul.h")) == std::errc::no_such_file_or_directory);
    ZEXPECT(status_error(at("bytes.h")) == std::errc::no_such_file_or_directory);
}

ZEST_CASE(a_target_names_a_character_win32_refuses_as_wsl_keeps_it) {
    // WSL and Cygwin keep `:` in a name as U+F03A, which a name the link
    // leads to must hold rather than open a stream of `a`.
    test::write_text(at("a\xEF\x80\xBA"
                        "b.h"),
                     "colon");
    test::make_wsl_symlink(at("colon.h"), "a:b.h");
    ZEXPECT(text_at(at("colon.h")) == "colon");
    ZEXPECT(!write_file(at("colon.h"), "again"));
    ZEXPECT(test::read_text(at("a\xEF\x80\xBA"
                               "b.h")) == "again");
    ZEXPECT(!std::filesystem::exists(dir.path / "a"));
}

ZEST_CASE(canonical_names_where_the_links_lead) {
    test::make_wsl_symlink(at("rel.h"), "real/h.h");
    test::make_wsl_symlink(at("dirlink"), "real");
    std::string resolved;
    ZEXPECT(!canonical(resolved, at("rel.h")));
    ZEXPECT(resolved == real("real/h.h"));
    ZEXPECT(!canonical(resolved, at("dirlink/sub/s.h")));
    ZEXPECT(resolved == real("real/sub/s.h"));
    ZEXPECT(!canonical(resolved, at("dirlink")));
    ZEXPECT(resolved == real("real"));
    // The part that exists, through the link, and the rest appended.
    weakly_canonical(resolved, at("dirlink/sub/missing.h"));
    ZEXPECT(resolved == real("real/sub") + R"(\missing.h)");
}

ZEST_CASE(a_directory_lists_a_link_as_a_symlink) {
    test::make_wsl_symlink(at("rel.h"), "real/h.h");
    test::make_wsl_symlink(at("dirlink"), "real");
    std::vector<std::string> links;
    for(auto& entry: test::entries_of(dir.root())) {
        ZEST_CONTEXT("{}", entry.name);
        if(entry.name == "real") {
            continue;
        }
        links.push_back(entry.name);
        ZEXPECT(entry.type == FileType::Symlink);
        auto itself = entry.status();
        ZASSERT(itself.has_value());
        ZEXPECT(itself->type == FileType::Symlink);
    }
    std::ranges::sort(links);
    ZEXPECT(links == std::vector<std::string>{"dirlink", "rel.h"});
    // Opened through the link, the directory lists what it leads to.
    std::vector<std::string> inside;
    for(auto& entry: test::entries_of(at("dirlink"))) {
        inside.push_back(entry.name);
    }
    std::ranges::sort(inside);
    ZEXPECT(inside == std::vector<std::string>{"h.h", "sub"});
}

ZEST_CASE(walk_goes_through_the_root_but_not_into_a_link) {
    test::make_wsl_symlink(at("dirlink"), "real");
    std::vector<std::string> walked;
    for(auto& entry: walk(dir.root())) {
        ZASSERT(entry.has_value());
        walked.push_back(std::string(entry->relative()));
    }
    std::ranges::sort(walked);
    ZEXPECT(walked == std::vector<std::string>{"dirlink",
                                               "real",
                                               R"(real\h.h)",
                                               R"(real\sub)",
                                               R"(real\sub\s.h)"});
    walked.clear();
    for(auto& entry: walk(at("dirlink"))) {
        ZASSERT(entry.has_value());
        walked.push_back(std::string(entry->relative()));
    }
    std::ranges::sort(walked);
    ZEXPECT(walked == std::vector<std::string>{"h.h", "sub", R"(sub\s.h)"});
}

ZEST_CASE(a_directory_through_a_link_opens_as_one) {
    test::make_wsl_symlink(at("dirlink"), "real");
    auto file = File::open(at("dirlink"));
    ZASSERT(!file);
    ZEXPECT(file.error() == std::errc::is_a_directory);
    ZEXPECT(volume(at("dirlink/sub")).has_value());
}

ZEST_CASE(remove_takes_the_link_itself_and_goes_through_one_on_the_way) {
    test::make_wsl_symlink(at("dirlink"), "real");
    test::make_wsl_symlink(at("rel.h"), "real/h.h");
    ZEXPECT(remove(at("dirlink/sub/s.h")) == true);
    ZEXPECT(!std::filesystem::exists(dir.path / "real" / "sub" / "s.h"));
    ZEXPECT(remove(at("rel.h")) == true);
    ZEXPECT(remove_all(at("dirlink")) == 1u);
    ZEXPECT(test::read_text(at("real/h.h")) == "h");
    ZEXPECT(std::filesystem::exists(dir.path / "real" / "sub"));
}

ZEST_CASE(rename_goes_through_a_link_on_the_way_from) {
    test::make_wsl_symlink(at("dirlink"), "real");
    ZEXPECT(!rename(at("dirlink/h.h"), at("moved.h")));
    ZEXPECT(test::read_text(at("moved.h")) == "h");
    ZEXPECT(!std::filesystem::exists(dir.path / "real" / "h.h"));
}

ZEST_CASE(a_file_is_made_through_a_link) {
    test::make_wsl_symlink(at("dirlink"), "real");
    test::make_wsl_symlink(at("dangling"), "made.txt");
    ZEXPECT(!write_file(at("dirlink/new.txt"), "new"));
    ZEXPECT(test::read_text(at("real/new.txt")) == "new");
    auto made = File::open(at("dirlink/sub/fresh.txt"), {.write = true, .create_new = true});
    ZEXPECT(made.has_value());
    ZEXPECT(std::filesystem::exists(dir.path / "real" / "sub" / "fresh.txt"));
    // As POSIX, an open that may create a file creates what a dangling link
    // names; one that must create it takes the link for a file there.
    ZEXPECT(!write_file(at("dangling"), "made"));
    ZEXPECT(test::read_text(at("made.txt")) == "made");
    auto again = File::open(at("dangling"), {.write = true, .create_new = true});
    ZASSERT(!again);
    ZEXPECT(again.error() == std::errc::file_exists);
}

};  // ZEST_SUITE(fs_windows_wsl_symlink)

}  // namespace

}  // namespace kotatsu::fs

#endif
