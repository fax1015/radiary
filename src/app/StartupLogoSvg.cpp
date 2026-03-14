#include "app/StartupLogoSvg.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>
#include <utility>

namespace {

struct SvgMatrix {
    float m11 = 1.0f;
    float m12 = 0.0f;
    float m21 = 0.0f;
    float m22 = 1.0f;
    float dx = 0.0f;
    float dy = 0.0f;
};

std::string TrimAscii(std::string value) {
    const auto isSpace = [](const unsigned char ch) {
        return std::isspace(ch) != 0;
    };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](const char ch) {
        return !isSpace(static_cast<unsigned char>(ch));
    }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [&](const char ch) {
        return !isSpace(static_cast<unsigned char>(ch));
    }).base(), value.end());
    return value;
}

bool ExtractXmlAttribute(const std::string& element, const char* name, std::string& value) {
    const std::regex regex(std::string(name) + "=\"([^\"]*)\"", std::regex::icase);
    std::smatch match;
    if (!std::regex_search(element, match, regex) || match.size() < 2) {
        return false;
    }
    value = match[1].str();
    return true;
}

bool ParseSvgColorText(const std::string& text, COLORREF& color) {
    const std::string trimmed = TrimAscii(text);
    std::smatch match;
    if (std::regex_search(trimmed, match, std::regex(R"(rgb\(\s*([0-9]{1,3})\s*,\s*([0-9]{1,3})\s*,\s*([0-9]{1,3})\s*\))", std::regex::icase))
        && match.size() >= 4) {
        const int r = std::clamp(std::stoi(match[1].str()), 0, 255);
        const int g = std::clamp(std::stoi(match[2].str()), 0, 255);
        const int b = std::clamp(std::stoi(match[3].str()), 0, 255);
        color = RGB(r, g, b);
        return true;
    }
    if (trimmed.size() == 7 && trimmed[0] == '#') {
        const int r = std::stoi(trimmed.substr(1, 2), nullptr, 16);
        const int g = std::stoi(trimmed.substr(3, 2), nullptr, 16);
        const int b = std::stoi(trimmed.substr(5, 2), nullptr, 16);
        color = RGB(r, g, b);
        return true;
    }
    return false;
}

bool ExtractSvgColor(const std::string& pathTag, COLORREF& color) {
    std::string value;
    if (ExtractXmlAttribute(pathTag, "stroke", value) && ParseSvgColorText(value, color)) {
        return true;
    }
    if (ExtractXmlAttribute(pathTag, "fill", value) && ParseSvgColorText(value, color)) {
        return true;
    }

    std::string style;
    if (!ExtractXmlAttribute(pathTag, "style", style)) {
        return false;
    }

    std::smatch match;
    if (std::regex_search(style, match, std::regex(R"(stroke\s*:\s*([^;]+))", std::regex::icase))
        && match.size() >= 2
        && ParseSvgColorText(match[1].str(), color)) {
        return true;
    }
    if (std::regex_search(style, match, std::regex(R"(fill\s*:\s*([^;]+))", std::regex::icase))
        && match.size() >= 2
        && ParseSvgColorText(match[1].str(), color)) {
        return true;
    }
    return false;
}

bool ParseSvgMatrixText(const std::string& text, SvgMatrix& matrix) {
    const char* cursor = text.c_str();
    float values[6] {};
    for (int index = 0; index < 6; ++index) {
        while (*cursor != '\0' && (std::isspace(static_cast<unsigned char>(*cursor)) != 0 || *cursor == ',')) {
            ++cursor;
        }
        if (*cursor == '\0') {
            return false;
        }
        char* end = nullptr;
        values[index] = std::strtof(cursor, &end);
        if (end == cursor) {
            return false;
        }
        cursor = end;
    }
    matrix.m11 = values[0];
    matrix.m12 = values[1];
    matrix.m21 = values[2];
    matrix.m22 = values[3];
    matrix.dx = values[4];
    matrix.dy = values[5];
    return true;
}

bool ParseSvgTransform(const std::string& text, SvgMatrix& matrix) {
    const std::string trimmed = TrimAscii(text);
    if (trimmed.size() < 9 || trimmed.rfind("matrix(", 0) != 0 || trimmed.back() != ')') {
        return false;
    }
    return ParseSvgMatrixText(trimmed.substr(7, trimmed.size() - 8), matrix);
}

SvgMatrix ComposeSvgMatrix(const SvgMatrix& parent, const SvgMatrix& child) {
    SvgMatrix result;
    result.m11 = parent.m11 * child.m11 + parent.m21 * child.m12;
    result.m12 = parent.m12 * child.m11 + parent.m22 * child.m12;
    result.m21 = parent.m11 * child.m21 + parent.m21 * child.m22;
    result.m22 = parent.m12 * child.m21 + parent.m22 * child.m22;
    result.dx = parent.m11 * child.dx + parent.m21 * child.dy + parent.dx;
    result.dy = parent.m12 * child.dx + parent.m22 * child.dy + parent.dy;
    return result;
}

radiary::StartupLogoPoint ApplySvgMatrix(const SvgMatrix& matrix, const float x, const float y) {
    radiary::StartupLogoPoint point {};
    point.x = matrix.m11 * x + matrix.m21 * y + matrix.dx;
    point.y = matrix.m12 * x + matrix.m22 * y + matrix.dy;
    return point;
}

void SkipSvgPathSeparators(const char*& cursor) {
    while (*cursor != '\0' && (std::isspace(static_cast<unsigned char>(*cursor)) != 0 || *cursor == ',')) {
        ++cursor;
    }
}

bool ParseSvgPathNumber(const char*& cursor, float& value) {
    SkipSvgPathSeparators(cursor);
    if (*cursor == '\0' || std::isalpha(static_cast<unsigned char>(*cursor)) != 0) {
        return false;
    }
    char* end = nullptr;
    value = std::strtof(cursor, &end);
    if (end == cursor) {
        return false;
    }
    cursor = end;
    return true;
}

bool PointsNearlyEqual(const radiary::StartupLogoPoint& left, const radiary::StartupLogoPoint& right) {
    return std::fabs(left.x - right.x) < 0.001f && std::fabs(left.y - right.y) < 0.001f;
}

void UpdateLogoBounds(const radiary::StartupLogoPoint& point, float& minX, float& minY, float& maxX, float& maxY, bool& hasBounds) {
    if (!hasBounds) {
        minX = maxX = point.x;
        minY = maxY = point.y;
        hasBounds = true;
        return;
    }
    minX = std::min(minX, point.x);
    minY = std::min(minY, point.y);
    maxX = std::max(maxX, point.x);
    maxY = std::max(maxY, point.y);
}

bool ParseSvgPathData(
    const std::string& pathData,
    const SvgMatrix& matrix,
    std::vector<radiary::StartupLogoStroke>& strokes,
    float& minX,
    float& minY,
    float& maxX,
    float& maxY) {
    strokes.clear();
    const char* cursor = pathData.c_str();
    char command = '\0';
    float currentX = 0.0f;
    float currentY = 0.0f;
    float startX = 0.0f;
    float startY = 0.0f;
    radiary::StartupLogoStroke currentStroke;
    bool hasBounds = false;

    const auto finishStroke = [&]() {
        if (!currentStroke.points.empty()) {
            strokes.push_back(std::move(currentStroke));
            currentStroke = radiary::StartupLogoStroke {};
        }
    };
    const auto beginStroke = [&](const float x, const float y) {
        finishStroke();
        currentStroke = radiary::StartupLogoStroke {};
        const radiary::StartupLogoPoint transformed = ApplySvgMatrix(matrix, x, y);
        currentStroke.points.push_back(transformed);
        UpdateLogoBounds(transformed, minX, minY, maxX, maxY, hasBounds);
        currentX = x;
        currentY = y;
        startX = x;
        startY = y;
    };
    const auto lineTo = [&](const float x, const float y) {
        if (currentStroke.points.empty()) {
            beginStroke(currentX, currentY);
        }
        const radiary::StartupLogoPoint transformed = ApplySvgMatrix(matrix, x, y);
        if (currentStroke.points.empty() || !PointsNearlyEqual(currentStroke.points.back(), transformed)) {
            currentStroke.points.push_back(transformed);
            UpdateLogoBounds(transformed, minX, minY, maxX, maxY, hasBounds);
        }
        currentX = x;
        currentY = y;
    };

    while (true) {
        SkipSvgPathSeparators(cursor);
        if (*cursor == '\0') {
            break;
        }
        if (std::isalpha(static_cast<unsigned char>(*cursor)) != 0) {
            command = *cursor++;
        } else if (command == '\0') {
            return false;
        }

        switch (command) {
        case 'M':
        case 'm': {
            float x = 0.0f;
            float y = 0.0f;
            bool firstPair = true;
            while (ParseSvgPathNumber(cursor, x) && ParseSvgPathNumber(cursor, y)) {
                if (command == 'm') {
                    x += currentX;
                    y += currentY;
                }
                if (firstPair) {
                    beginStroke(x, y);
                    firstPair = false;
                } else {
                    lineTo(x, y);
                }
                SkipSvgPathSeparators(cursor);
                if (*cursor != '\0' && std::isalpha(static_cast<unsigned char>(*cursor)) != 0) {
                    break;
                }
            }
            break;
        }
        case 'L':
        case 'l': {
            float x = 0.0f;
            float y = 0.0f;
            while (ParseSvgPathNumber(cursor, x) && ParseSvgPathNumber(cursor, y)) {
                if (command == 'l') {
                    x += currentX;
                    y += currentY;
                }
                lineTo(x, y);
                SkipSvgPathSeparators(cursor);
                if (*cursor != '\0' && std::isalpha(static_cast<unsigned char>(*cursor)) != 0) {
                    break;
                }
            }
            break;
        }
        case 'H':
        case 'h': {
            float x = 0.0f;
            while (ParseSvgPathNumber(cursor, x)) {
                if (command == 'h') {
                    x += currentX;
                }
                lineTo(x, currentY);
                SkipSvgPathSeparators(cursor);
                if (*cursor != '\0' && std::isalpha(static_cast<unsigned char>(*cursor)) != 0) {
                    break;
                }
            }
            break;
        }
        case 'V':
        case 'v': {
            float y = 0.0f;
            while (ParseSvgPathNumber(cursor, y)) {
                if (command == 'v') {
                    y += currentY;
                }
                lineTo(currentX, y);
                SkipSvgPathSeparators(cursor);
                if (*cursor != '\0' && std::isalpha(static_cast<unsigned char>(*cursor)) != 0) {
                    break;
                }
            }
            break;
        }
        case 'C':
        case 'c': {
            float x1 = 0.0f;
            float y1 = 0.0f;
            float x2 = 0.0f;
            float y2 = 0.0f;
            float x = 0.0f;
            float y = 0.0f;
            while (ParseSvgPathNumber(cursor, x1)
                && ParseSvgPathNumber(cursor, y1)
                && ParseSvgPathNumber(cursor, x2)
                && ParseSvgPathNumber(cursor, y2)
                && ParseSvgPathNumber(cursor, x)
                && ParseSvgPathNumber(cursor, y)) {
                if (command == 'c') {
                    x1 += currentX;
                    y1 += currentY;
                    x2 += currentX;
                    y2 += currentY;
                    x += currentX;
                    y += currentY;
                }

                const float p0x = currentX;
                const float p0y = currentY;
                const float estimate = std::hypot(x1 - p0x, y1 - p0y)
                    + std::hypot(x2 - x1, y2 - y1)
                    + std::hypot(x - x2, y - y2);
                const int steps = std::clamp(static_cast<int>(std::ceil(estimate / 18.0f)), 12, 64);
                for (int step = 1; step <= steps; ++step) {
                    const float t = static_cast<float>(step) / static_cast<float>(steps);
                    const float invT = 1.0f - t;
                    const float curveX =
                        invT * invT * invT * p0x
                        + 3.0f * invT * invT * t * x1
                        + 3.0f * invT * t * t * x2
                        + t * t * t * x;
                    const float curveY =
                        invT * invT * invT * p0y
                        + 3.0f * invT * invT * t * y1
                        + 3.0f * invT * t * t * y2
                        + t * t * t * y;
                    lineTo(curveX, curveY);
                }

                SkipSvgPathSeparators(cursor);
                if (*cursor != '\0' && std::isalpha(static_cast<unsigned char>(*cursor)) != 0) {
                    break;
                }
            }
            break;
        }
        case 'Z':
        case 'z':
            if (!currentStroke.points.empty()) {
                const radiary::StartupLogoPoint startPoint = ApplySvgMatrix(matrix, startX, startY);
                if (!PointsNearlyEqual(currentStroke.points.back(), startPoint)) {
                    currentStroke.points.push_back(startPoint);
                    UpdateLogoBounds(startPoint, minX, minY, maxX, maxY, hasBounds);
                }
                currentStroke.closed = true;
                currentX = startX;
                currentY = startY;
            }
            break;
        default:
            return false;
        }
    }

    finishStroke();
    return hasBounds && !strokes.empty();
}

}  // namespace

namespace radiary {

bool LoadStartupLogoSvg(const std::filesystem::path& path, StartupLogoSvgData& output) {
    output = StartupLogoSvgData {};

    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return false;
    }

    const std::string svg((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    if (svg.empty()) {
        return false;
    }

    std::vector<SvgMatrix> transformStack(1);
    bool hasBounds = false;
    std::size_t bestColorWeight = 0;
    const std::regex tagRegex(R"(<\s*(/?)\s*(g|path)\b([^>]*)>)", std::regex::icase);

    for (std::sregex_iterator iterator(svg.begin(), svg.end(), tagRegex), end; iterator != end; ++iterator) {
        const std::smatch& match = *iterator;
        const bool closingTag = !match[1].str().empty();
        std::string tagName = match[2].str();
        std::transform(tagName.begin(), tagName.end(), tagName.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        const std::string tagText = match[0].str();
        const bool selfClosing = !closingTag && tagText.size() >= 2 && tagText[tagText.size() - 2] == '/';

        if (tagName == "g") {
            if (closingTag) {
                if (transformStack.size() > 1) {
                    transformStack.pop_back();
                }
                continue;
            }

            SvgMatrix groupTransform;
            std::string transformText;
            if (ExtractXmlAttribute(tagText, "transform", transformText) && ParseSvgTransform(transformText, groupTransform)) {
                transformStack.push_back(ComposeSvgMatrix(transformStack.back(), groupTransform));
            } else {
                transformStack.push_back(transformStack.back());
            }

            if (selfClosing && transformStack.size() > 1) {
                transformStack.pop_back();
            }
            continue;
        }

        if (tagName != "path" || closingTag) {
            continue;
        }

        std::string pathData;
        if (!ExtractXmlAttribute(tagText, "d", pathData)) {
            continue;
        }

        SvgMatrix pathTransform = transformStack.back();
        std::string transformText;
        SvgMatrix localTransform;
        if (ExtractXmlAttribute(tagText, "transform", transformText) && ParseSvgTransform(transformText, localTransform)) {
            pathTransform = ComposeSvgMatrix(pathTransform, localTransform);
        }

        std::vector<StartupLogoStroke> pathStrokes;
        float minX = 0.0f;
        float minY = 0.0f;
        float maxX = 0.0f;
        float maxY = 0.0f;
        if (!ParseSvgPathData(pathData, pathTransform, pathStrokes, minX, minY, maxX, maxY)) {
            continue;
        }

        COLORREF pathColor = output.color;
        ExtractSvgColor(tagText, pathColor);

        std::size_t pathPointCount = 0;
        for (const StartupLogoStroke& stroke : pathStrokes) {
            pathPointCount += stroke.points.size();
        }
        if (pathPointCount > bestColorWeight) {
            bestColorWeight = pathPointCount;
            output.color = pathColor;
        }

        if (!hasBounds) {
            output.minX = minX;
            output.minY = minY;
            output.maxX = maxX;
            output.maxY = maxY;
            hasBounds = true;
        } else {
            output.minX = std::min(output.minX, minX);
            output.minY = std::min(output.minY, minY);
            output.maxX = std::max(output.maxX, maxX);
            output.maxY = std::max(output.maxY, maxY);
        }

        output.strokes.insert(output.strokes.end(), pathStrokes.begin(), pathStrokes.end());
    }

    return hasBounds && !output.strokes.empty();
}

}  // namespace radiary
