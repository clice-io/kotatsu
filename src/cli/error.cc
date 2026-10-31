#include "kotatsu/cli/error.h"

#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ansi.h"

namespace kotatsu::cli {

namespace {

/// The argv elements joined by spaces, and where each starts in the line.
struct JoinedArgv {
    std::string line;
    std::vector<std::size_t> starts;
};

JoinedArgv join_argv(const std::vector<std::string>& argv) {
    JoinedArgv joined;
    for(const auto& argument: argv) {
        if(!joined.line.empty() || !joined.starts.empty()) {
            joined.line.push_back(' ');
        }
        joined.starts.push_back(joined.line.size());
        joined.line += argument;
    }
    return joined;
}

/// The line an error shows, cut to a width, and its mark: where it starts and how wide.
struct Excerpt {
    std::string line;
    std::size_t marker_start = 0;
    std::size_t marker_width = 1;
};

/// `line` cut to `max_width` around the mark, with an ellipsis where it is cut; 0 keeps it
/// whole.
Excerpt excerpt(std::string_view line,
                std::size_t marker_start,
                std::size_t marker_width,
                std::size_t max_width) {
    Excerpt result{
        .line = std::string(line),
        .marker_start = marker_start,
        .marker_width = marker_width,
    };
    if(max_width == 0 || line.size() <= max_width) {
        return result;
    }

    constexpr std::string_view ellipsis = "...";
    if(max_width <= ellipsis.size()) {
        result.line = std::string(ellipsis.substr(0, max_width));
        result.marker_start = 0;
        result.marker_width = 1;
        return result;
    }

    // Up to 16 characters before the mark, fewer when the width leaves room for less between
    // an ellipsis on either side, so that the mark stays in view.
    const std::size_t between_ellipses =
        max_width > 2 * ellipsis.size() ? max_width - 2 * ellipsis.size() : 0;
    const std::size_t context_before_marker =
        std::min<std::size_t>(16, between_ellipses == 0 ? 0 : between_ellipses - 1);

    std::size_t content_start = 0;
    if(marker_start >= line.size()) {
        content_start = line.size() - (max_width - ellipsis.size());
    } else if(marker_start > context_before_marker) {
        content_start = marker_start - context_before_marker;
    }

    const bool crop_left = content_start > 0;
    std::size_t available = max_width;
    if(crop_left) {
        available -= ellipsis.size();
    }
    std::size_t content_end = std::min(line.size(), content_start + available);
    bool crop_right = content_end < line.size();
    if(crop_right) {
        if(available > ellipsis.size()) {
            available -= ellipsis.size();
            content_end = content_start + available;
        } else {
            // No room for a second ellipsis: the line just stops.
            crop_right = false;
        }
    }
    if(content_end == line.size()) {
        // The mark is near the end, and the window ends there: fill it from the end back.
        content_start = line.size() - available;
    }

    std::string cut;
    if(crop_left) {
        cut += ellipsis;
    }
    cut += line.substr(content_start, content_end - content_start);
    if(crop_right) {
        cut += ellipsis;
    }

    const std::size_t visible_start =
        marker_start <= content_start ? 0 : marker_start - content_start;
    const std::size_t visible_width =
        content_end > marker_start ? content_end - std::max(marker_start, content_start) : 0;
    result.line = std::move(cut);
    result.marker_start = (crop_left ? ellipsis.size() : 0) + visible_start;
    result.marker_width = std::max<std::size_t>(1, std::min(marker_width, visible_width));
    return result;
}

/// `text` with [start, start + width) in `ansi`.
std::string
    highlight(std::string_view text, std::size_t start, std::size_t width, std::string_view ansi) {
    if(start >= text.size()) {
        return std::string(text);
    }
    width = std::min(width, text.size() - start);
    return std::format("{}{}{}{}{}",
                       text.substr(0, start),
                       ansi,
                       text.substr(start, width),
                       detail::ansi_reset,
                       text.substr(start + width));
}

}  // namespace

std::string Error::render(const Style& style) const {
    if(argv.empty()) {
        return style.color ? std::format("{} {}",
                                         detail::paint(detail::ansi_error, "error"),
                                         detail::paint(detail::ansi_bold, message))
                           : message;
    }

    const auto argc = static_cast<std::uint32_t>(argv.size());
    const auto first = std::min(begin, argc);
    const bool at_end = first == argc;
    const auto last = std::max(std::min(end, argc), first + 1);

    const auto joined = join_argv(argv);
    std::size_t marker_start = joined.line.size();
    std::size_t marker_width = 1;
    if(!at_end) {
        marker_start = joined.starts[first];
        // An empty argument is marked where it stands, one column wide.
        marker_width =
            std::max<std::size_t>(1,
                                  joined.starts[last - 1] + argv[last - 1].size() - marker_start);
    }
    const auto shown = excerpt(joined.line, marker_start, marker_width, style.argv_width);

    if(!style.color) {
        std::string marker(shown.marker_start, ' ');
        marker.push_back('^');
        marker.append(shown.marker_width - 1, '~');
        return std::format("{}:\n  {}\n  {}\n  {}",
                           at_end ? "at end of argv" : std::format("at argv[{}]", first),
                           shown.line,
                           marker,
                           message);
    }

    using namespace detail;
    return std::format("{} {}\n{}\n{} {}\n{} {}{} {}",
                       paint(ansi_error, "error"),
                       paint(ansi_help, at_end ? "[argv[end]]" : std::format("[argv[{}]]", first)),
                       paint(ansi_help, "│"),
                       paint(ansi_help, "│"),
                       highlight(shown.line, shown.marker_start, shown.marker_width, ansi_syntax),
                       paint(ansi_help, "│"),
                       std::string(shown.marker_start, ' '),
                       paint(ansi_marker, "╰─▶"),
                       paint(ansi_bold, message));
}

Error detail::error_at(Error::Kind kind,
                       std::string message,
                       string_list_ref argv,
                       std::uint32_t begin,
                       std::uint32_t end) {
    return Error{
        .kind = kind,
        .message = std::move(message),
        .argv = std::vector<std::string>(argv.begin(), argv.end()),
        .begin = begin,
        .end = end,
    };
}

}  // namespace kotatsu::cli
