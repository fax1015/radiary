#include "io/SceneSerializer.h"

#include <cctype>
#include <fstream>
#include <locale>
#include <map>
#include <sstream>
#include <string_view>
#include <utility>

namespace radiary {

namespace {

struct JsonValue {
    enum class Type {
        Null,
        Bool,
        Number,
        String,
        Object,
        Array
    };

    Type type = Type::Null;
    bool boolValue = false;
    double numberValue = 0.0;
    std::string stringValue;
    std::map<std::string, JsonValue> objectValue;
    std::vector<JsonValue> arrayValue;

    const JsonValue* Find(const std::string& key) const {
        const auto iterator = objectValue.find(key);
        return iterator == objectValue.end() ? nullptr : &iterator->second;
    }
};

class JsonParser {
public:
    explicit JsonParser(std::string_view input)
        : input_(input) {}

    bool Parse(JsonValue& value, std::string& error) {
        SkipWhitespace();
        if (position_ < input_.size()
            && static_cast<unsigned char>(input_[position_]) == 0xEF
            && position_ + 2 < input_.size()
            && static_cast<unsigned char>(input_[position_ + 1]) == 0xBB
            && static_cast<unsigned char>(input_[position_ + 2]) == 0xBF) {
            position_ += 3;
            SkipWhitespace();
        }
        if (!ParseValue(value, error)) {
            return false;
        }
        SkipWhitespace();
        if (position_ != input_.size()) {
            error = "Unexpected content after JSON document";
            return false;
        }
        return true;
    }

private:
    std::string ErrorHere(const std::string& message) const {
        const std::size_t begin = position_ > 16 ? position_ - 16 : 0;
        const std::size_t end = std::min(input_.size(), position_ + 24);
        std::string snippet(input_.substr(begin, end - begin));
        for (char& character : snippet) {
            if (character == '\n' || character == '\r' || character == '\t') {
                character = ' ';
            }
        }
        return message + " at " + std::to_string(position_) + " near `" + snippet + "`";
    }

    bool ParseValue(JsonValue& value, std::string& error) {
        if (position_ >= input_.size()) {
            error = ErrorHere("Unexpected end of input");
            return false;
        }

        switch (input_[position_]) {
        case '{':
            return ParseObject(value, error);
        case '[':
            return ParseArray(value, error);
        case '"':
            value.type = JsonValue::Type::String;
            return ParseString(value.stringValue, error);
        case 't':
            return ParseLiteral("true", value, true, error);
        case 'f':
            return ParseLiteral("false", value, false, error);
        case 'n':
            return ParseNull(value, error);
        default:
            return ParseNumber(value, error);
        }
    }

    bool ParseObject(JsonValue& value, std::string& error) {
        value.type = JsonValue::Type::Object;
        ++position_;
        SkipWhitespace();
        if (Match('}')) {
            return true;
        }

        while (position_ < input_.size()) {
            std::string key;
            if (!ParseString(key, error)) {
                return false;
            }
            SkipWhitespace();
            if (!Match(':')) {
                error = ErrorHere("Expected ':' in object");
                return false;
            }
            SkipWhitespace();
            JsonValue member;
            if (!ParseValue(member, error)) {
                return false;
            }
            value.objectValue.emplace(std::move(key), std::move(member));
            SkipWhitespace();
            if (Match('}')) {
                return true;
            }
            if (!Match(',')) {
                error = ErrorHere("Expected ',' in object");
                return false;
            }
            SkipWhitespace();
        }

        error = ErrorHere("Unterminated object");
        return false;
    }

    bool ParseArray(JsonValue& value, std::string& error) {
        value.type = JsonValue::Type::Array;
        ++position_;
        SkipWhitespace();
        if (Match(']')) {
            return true;
        }

        while (position_ < input_.size()) {
            JsonValue element;
            if (!ParseValue(element, error)) {
                return false;
            }
            value.arrayValue.push_back(std::move(element));
            SkipWhitespace();
            if (Match(']')) {
                return true;
            }
            if (!Match(',')) {
                error = ErrorHere("Expected ',' in array");
                return false;
            }
            SkipWhitespace();
        }

        error = ErrorHere("Unterminated array");
        return false;
    }

    bool ParseString(std::string& value, std::string& error) {
        if (!Match('"')) {
            error = ErrorHere("Expected string");
            return false;
        }

        std::ostringstream stream;
        while (position_ < input_.size()) {
            const char character = input_[position_++];
            if (character == '"') {
                value = stream.str();
                return true;
            }
            if (character == '\\') {
                if (position_ >= input_.size()) {
                    error = ErrorHere("Invalid escape sequence");
                    return false;
                }
                const char escaped = input_[position_++];
                switch (escaped) {
                case '"':
                case '\\':
                case '/':
                    stream << escaped;
                    break;
                case 'b':
                    stream << '\b';
                    break;
                case 'f':
                    stream << '\f';
                    break;
                case 'n':
                    stream << '\n';
                    break;
                case 'r':
                    stream << '\r';
                    break;
                case 't':
                    stream << '\t';
                    break;
                default:
                    error = ErrorHere("Unsupported escape sequence");
                    return false;
                }
            } else {
                stream << character;
            }
        }

        error = ErrorHere("Unterminated string");
        return false;
    }

    bool ParseLiteral(const char* literal, JsonValue& value, const bool booleanValue, std::string& error) {
        const std::size_t length = std::char_traits<char>::length(literal);
        if (input_.substr(position_, length) != literal) {
            error = ErrorHere("Invalid literal");
            return false;
        }
        position_ += length;
        value.type = JsonValue::Type::Bool;
        value.boolValue = booleanValue;
        return true;
    }

    bool ParseNull(JsonValue& value, std::string& error) {
        if (input_.substr(position_, 4) != "null") {
            error = ErrorHere("Invalid null literal");
            return false;
        }
        position_ += 4;
        value.type = JsonValue::Type::Null;
        return true;
    }

    bool ParseNumber(JsonValue& value, std::string& error) {
        const std::size_t start = position_;
        if (position_ < input_.size() && (input_[position_] == '-' || input_[position_] == '+')) {
            ++position_;
        }
        const std::size_t integerStart = position_;
        while (position_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[position_]))) {
            ++position_;
        }
        const bool hasIntegerDigits = position_ > integerStart;
        bool hasFractionDigits = false;
        if (position_ < input_.size() && input_[position_] == '.') {
            ++position_;
            const std::size_t fractionStart = position_;
            while (position_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[position_]))) {
                ++position_;
            }
            hasFractionDigits = position_ > fractionStart;
        }
        if (!hasIntegerDigits && !hasFractionDigits) {
            error = ErrorHere("Invalid number");
            return false;
        }
        if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) {
            ++position_;
            if (position_ < input_.size() && (input_[position_] == '-' || input_[position_] == '+')) {
                ++position_;
            }
            const std::size_t exponentStart = position_;
            while (position_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[position_]))) {
                ++position_;
            }
            if (exponentStart == position_) {
                error = ErrorHere("Invalid exponent in number");
                return false;
            }
        }

        try {
            value.type = JsonValue::Type::Number;
            const std::string token(input_.substr(start, position_ - start));
            std::istringstream stream(token);
            stream.imbue(std::locale::classic());
            stream >> value.numberValue;
            char trailing = '\0';
            if (!stream || (stream >> trailing)) {
                error = ErrorHere("Invalid number");
                return false;
            }
            return true;
        } catch (...) {
            error = ErrorHere("Invalid number");
            return false;
        }
    }

    bool Match(const char expected) {
        if (position_ < input_.size() && input_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    void SkipWhitespace() {
        while (position_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[position_]))) {
            ++position_;
        }
    }

    std::string input_;
    std::size_t position_ = 0;
};

std::string EscapeJson(const std::string& value) {
    std::ostringstream stream;
    for (const char character : value) {
        switch (character) {
        case '\\':
            stream << "\\\\";
            break;
        case '"':
            stream << "\\\"";
            break;
        case '\n':
            stream << "\\n";
            break;
        case '\r':
            stream << "\\r";
            break;
        case '\t':
            stream << "\\t";
            break;
        default:
            stream << character;
            break;
        }
    }
    return stream.str();
}

double Number(const JsonValue& object, const std::string& key, const double fallback) {
    const JsonValue* value = object.Find(key);
    return (value && value->type == JsonValue::Type::Number) ? value->numberValue : fallback;
}

std::string String(const JsonValue& object, const std::string& key, const std::string& fallback) {
    const JsonValue* value = object.Find(key);
    return (value && value->type == JsonValue::Type::String) ? value->stringValue : fallback;
}

bool Boolean(const JsonValue& object, const std::string& key, const bool fallback) {
    const JsonValue* value = object.Find(key);
    return (value && value->type == JsonValue::Type::Bool) ? value->boolValue : fallback;
}

Vec3 ParseVec3(const JsonValue& value) {
    return {
        Number(value, "x", 0.0),
        Number(value, "y", 0.0),
        Number(value, "z", 0.0)
    };
}

Color ParseColor(const JsonValue& value) {
    return {
        static_cast<std::uint8_t>(Number(value, "r", 0.0)),
        static_cast<std::uint8_t>(Number(value, "g", 0.0)),
        static_cast<std::uint8_t>(Number(value, "b", 0.0)),
        static_cast<std::uint8_t>(Number(value, "a", 255.0))
    };
}

void WriteCameraState(std::ostream& stream, const CameraState& camera, const std::string& indent) {
    stream << indent << "\"yaw\": " << camera.yaw << ",\n";
    stream << indent << "\"pitch\": " << camera.pitch << ",\n";
    stream << indent << "\"distance\": " << camera.distance << ",\n";
    stream << indent << "\"panX\": " << camera.panX << ",\n";
    stream << indent << "\"panY\": " << camera.panY << ",\n";
    stream << indent << "\"zoom2D\": " << camera.zoom2D << ",\n";
    stream << indent << "\"frameWidth\": " << camera.frameWidth << ",\n";
    stream << indent << "\"frameHeight\": " << camera.frameHeight << "\n";
}

void WriteFlameRenderSettings(std::ostream& stream, const FlameRenderSettings& flameRender, const std::string& indent) {
    stream << indent << "\"rotationXDegrees\": " << flameRender.rotationXDegrees << ",\n";
    stream << indent << "\"rotationYDegrees\": " << flameRender.rotationYDegrees << ",\n";
    stream << indent << "\"rotationZDegrees\": " << flameRender.rotationZDegrees << ",\n";
    stream << indent << "\"depthAmount\": " << flameRender.depthAmount << ",\n";
    stream << indent << "\"symmetry\": \"" << ToString(flameRender.symmetry) << "\",\n";
    stream << indent << "\"symmetryOrder\": " << flameRender.symmetryOrder << ",\n";
    stream << indent << "\"curveExposure\": " << flameRender.curveExposure << ",\n";
    stream << indent << "\"curveContrast\": " << flameRender.curveContrast << ",\n";
    stream << indent << "\"curveHighlights\": " << flameRender.curveHighlights << ",\n";
    stream << indent << "\"curveGamma\": " << flameRender.curveGamma << "\n";
}

void WriteDepthOfFieldSettings(std::ostream& stream, const DepthOfFieldSettings& depthOfField, const std::string& indent) {
    stream << indent << "\"enabled\": " << (depthOfField.enabled ? "true" : "false") << ",\n";
    stream << indent << "\"focusDepth\": " << depthOfField.focusDepth << ",\n";
    stream << indent << "\"focusRange\": " << depthOfField.focusRange << ",\n";
    stream << indent << "\"blurStrength\": " << depthOfField.blurStrength << "\n";
}

void WriteDenoiserSettings(std::ostream& stream, const DenoiserSettings& denoiser, const std::string& indent) {
    stream << indent << "\"enabled\": " << (denoiser.enabled ? "true" : "false") << ",\n";
    stream << indent << "\"strength\": " << denoiser.strength << "\n";
}

void WritePostProcessSettings(std::ostream& stream, const PostProcessSettings& pp, const std::string& indent) {
    stream << indent << "\"enabled\": " << (pp.enabled ? "true" : "false") << ",\n";
    stream << indent << "\"bloomIntensity\": " << pp.bloomIntensity << ",\n";
    stream << indent << "\"bloomRadius\": " << pp.bloomRadius << ",\n";
    stream << indent << "\"bloomThreshold\": " << pp.bloomThreshold << ",\n";
    stream << indent << "\"chromaticAberration\": " << pp.chromaticAberration << ",\n";
    stream << indent << "\"vignetteIntensity\": " << pp.vignetteIntensity << ",\n";
    stream << indent << "\"vignetteRoundness\": " << pp.vignetteRoundness << ",\n";
    stream << indent << "\"acesToneMap\": " << (pp.acesToneMap ? "true" : "false") << ",\n";
    stream << indent << "\"filmGrain\": " << pp.filmGrain << ",\n";
    stream << indent << "\"colorTemperature\": " << pp.colorTemperature << ",\n";
    stream << indent << "\"saturationBoost\": " << pp.saturationBoost << "\n";
}

void WriteColor(std::ostream& stream, const Color& color) {
    stream << "{\"r\": " << static_cast<int>(color.r)
           << ", \"g\": " << static_cast<int>(color.g)
           << ", \"b\": " << static_cast<int>(color.b)
           << ", \"a\": " << static_cast<int>(color.a) << "}";
}

void WriteGradientStops(std::ostream& stream, const std::vector<GradientStop>& stops, const std::string& indent) {
    for (std::size_t index = 0; index < stops.size(); ++index) {
        const GradientStop& stop = stops[index];
        stream << indent << "{\"position\": " << stop.position
               << ", \"r\": " << static_cast<int>(stop.color.r)
               << ", \"g\": " << static_cast<int>(stop.color.g)
               << ", \"b\": " << static_cast<int>(stop.color.b)
               << ", \"a\": " << static_cast<int>(stop.color.a) << "}";
        stream << (index + 1 < stops.size() ? ",\n" : "\n");
    }
}

void WriteTransforms(std::ostream& stream, const std::vector<TransformLayer>& transforms, const std::string& indent) {
    for (std::size_t transformIndex = 0; transformIndex < transforms.size(); ++transformIndex) {
        const TransformLayer& layer = transforms[transformIndex];
        stream << indent << "{\n";
        stream << indent << "  \"name\": \"" << EscapeJson(layer.name) << "\",\n";
        stream << indent << "  \"weight\": " << layer.weight << ",\n";
        stream << indent << "  \"rotationDegrees\": " << layer.rotationDegrees << ",\n";
        stream << indent << "  \"scaleX\": " << layer.scaleX << ",\n";
        stream << indent << "  \"scaleY\": " << layer.scaleY << ",\n";
        stream << indent << "  \"translateX\": " << layer.translateX << ",\n";
        stream << indent << "  \"translateY\": " << layer.translateY << ",\n";
        stream << indent << "  \"shearX\": " << layer.shearX << ",\n";
        stream << indent << "  \"shearY\": " << layer.shearY << ",\n";
        stream << indent << "  \"colorIndex\": " << layer.colorIndex << ",\n";
        stream << indent << "  \"useCustomColor\": " << (layer.useCustomColor ? "true" : "false") << ",\n";
        stream << indent << "  \"customColor\": {\"r\": " << static_cast<int>(layer.customColor.r)
               << ", \"g\": " << static_cast<int>(layer.customColor.g)
               << ", \"b\": " << static_cast<int>(layer.customColor.b)
               << ", \"a\": " << static_cast<int>(layer.customColor.a) << "},\n";
        stream << indent << "  \"variations\": {\n";
        for (std::size_t variationIndex = 0; variationIndex < kVariationCount; ++variationIndex) {
            stream << indent << "    \"" << ToString(static_cast<VariationType>(variationIndex)) << "\": " << layer.variations[variationIndex];
            stream << (variationIndex + 1 < kVariationCount ? ",\n" : "\n");
        }
        stream << indent << "  }\n";
        stream << indent << "}";
        stream << (transformIndex + 1 < transforms.size() ? ",\n" : "\n");
    }
}

void WritePathSettings(std::ostream& stream, const PathSettings& path, const std::string& indent) {
    stream << indent << "{\n";
    stream << indent << "  \"name\": \"" << EscapeJson(path.name) << "\",\n";
    stream << indent << "  \"closed\": " << (path.closed ? "true" : "false") << ",\n";
    stream << indent << "  \"thickness\": " << path.thickness << ",\n";
    stream << indent << "  \"taper\": " << path.taper << ",\n";
    stream << indent << "  \"twist\": " << path.twist << ",\n";
    stream << indent << "  \"repeatCount\": " << path.repeatCount << ",\n";
    stream << indent << "  \"sampleCount\": " << path.sampleCount << ",\n";
    stream << indent << "  \"segment\": {\n";
    stream << indent << "    \"mode\": \"" << ToString(path.segment.mode) << "\",\n";
    stream << indent << "    \"segments\": " << path.segment.segments << ",\n";
    stream << indent << "    \"sides\": " << path.segment.sides << ",\n";
    stream << indent << "    \"breakSides\": " << (path.segment.breakSides ? "true" : "false") << ",\n";
    stream << indent << "    \"chamfer\": " << (path.segment.chamfer ? "true" : "false") << ",\n";
    stream << indent << "    \"chamferSize\": " << path.segment.chamferSize << ",\n";
    stream << indent << "    \"caps\": " << (path.segment.caps ? "true" : "false") << ",\n";
    stream << indent << "    \"size\": " << path.segment.size << ",\n";
    stream << indent << "    \"sizeX\": " << path.segment.sizeX << ",\n";
    stream << indent << "    \"sizeY\": " << path.segment.sizeY << ",\n";
    stream << indent << "    \"sizeZ\": " << path.segment.sizeZ << ",\n";
    stream << indent << "    \"orientToPath\": " << (path.segment.orientToPath ? "true" : "false") << ",\n";
    stream << indent << "    \"orientReferenceAxis\": \"" << ToString(path.segment.orientReferenceAxis) << "\",\n";
    stream << indent << "    \"rotateX\": " << path.segment.rotateX << ",\n";
    stream << indent << "    \"rotateY\": " << path.segment.rotateY << ",\n";
    stream << indent << "    \"rotateZ\": " << path.segment.rotateZ << ",\n";
    stream << indent << "    \"twistZ\": " << path.segment.twistZ << ",\n";
    stream << indent << "    \"debugNormals\": " << (path.segment.debugNormals ? "true" : "false") << ",\n";
    stream << indent << "    \"tessellate\": \"" << ToString(path.segment.tessellate) << "\",\n";
    stream << indent << "    \"randomness\": " << path.segment.randomness << ",\n";
    // Metalheart additions
    stream << indent << "    \"thicknessProfile\": \"" << ToString(path.segment.thicknessProfile) << "\",\n";
    stream << indent << "    \"thicknessPulseFrequency\": " << path.segment.thicknessPulseFrequency << ",\n";
    stream << indent << "    \"thicknessPulseDepth\": " << path.segment.thicknessPulseDepth << ",\n";
    stream << indent << "    \"thicknessBlobCenter\": " << path.segment.thicknessBlobCenter << ",\n";
    stream << indent << "    \"thicknessBlobWidth\": " << path.segment.thicknessBlobWidth << ",\n";
    stream << indent << "    \"junctionSize\": " << path.segment.junctionSize << ",\n";
    stream << indent << "    \"junctionBlend\": " << path.segment.junctionBlend << ",\n";
    stream << indent << "    \"tubeWarp\": " << path.segment.tubeWarp << ",\n";
    stream << indent << "    \"tubeWarpFrequency\": " << path.segment.tubeWarpFrequency << ",\n";
    stream << indent << "    \"tendrilCount\": " << path.segment.tendrilCount << ",\n";
    stream << indent << "    \"tendrilLength\": " << path.segment.tendrilLength << ",\n";
    stream << indent << "    \"tendrilThickness\": " << path.segment.tendrilThickness << ",\n";
    stream << indent << "    \"tendrilWarp\": " << path.segment.tendrilWarp << "\n";
    stream << indent << "  },\n";
    stream << indent << "  \"fractalDisplacement\": {\n";
    stream << indent << "    \"fractalType\": \"" << ToString(path.fractalDisplacement.fractalType) << "\",\n";
    stream << indent << "    \"space\": \"" << ToString(path.fractalDisplacement.space) << "\",\n";
    stream << indent << "    \"amplitude\": " << path.fractalDisplacement.amplitude << ",\n";
    stream << indent << "    \"frequency\": " << path.fractalDisplacement.frequency << ",\n";
    stream << indent << "    \"evolution\": " << path.fractalDisplacement.evolution << ",\n";
    stream << indent << "    \"offsetX\": " << path.fractalDisplacement.offsetX << ",\n";
    stream << indent << "    \"offsetY\": " << path.fractalDisplacement.offsetY << ",\n";
    stream << indent << "    \"offsetZ\": " << path.fractalDisplacement.offsetZ << ",\n";
    stream << indent << "    \"complexity\": " << path.fractalDisplacement.complexity << ",\n";
    stream << indent << "    \"octScale\": " << path.fractalDisplacement.octScale << ",\n";
    stream << indent << "    \"octMult\": " << path.fractalDisplacement.octMult << ",\n";
    stream << indent << "    \"smoothenNormals\": " << path.fractalDisplacement.smoothenNormals << ",\n";
    stream << indent << "    \"seamlessLoop\": " << (path.fractalDisplacement.seamlessLoop ? "true" : "false") << "\n";
    stream << indent << "  },\n";
    stream << indent << "  \"material\": {\n";
    stream << indent << "    \"renderMode\": \"" << ToString(path.material.renderMode) << "\",\n";
    stream << indent << "    \"materialType\": \"" << ToString(path.material.materialType) << "\",\n";
    stream << indent << "    \"primaryColor\": {\"r\": " << static_cast<int>(path.material.primaryColor.r)
           << ", \"g\": " << static_cast<int>(path.material.primaryColor.g)
           << ", \"b\": " << static_cast<int>(path.material.primaryColor.b)
           << ", \"a\": " << static_cast<int>(path.material.primaryColor.a) << "},\n";
    stream << indent << "    \"accentColor\": {\"r\": " << static_cast<int>(path.material.accentColor.r)
           << ", \"g\": " << static_cast<int>(path.material.accentColor.g)
           << ", \"b\": " << static_cast<int>(path.material.accentColor.b)
           << ", \"a\": " << static_cast<int>(path.material.accentColor.a) << "},\n";
    stream << indent << "    \"wireColor\": {\"r\": " << static_cast<int>(path.material.wireColor.r)
           << ", \"g\": " << static_cast<int>(path.material.wireColor.g)
           << ", \"b\": " << static_cast<int>(path.material.wireColor.b)
           << ", \"a\": " << static_cast<int>(path.material.wireColor.a) << "},\n";
    stream << indent << "    \"pointSize\": " << path.material.pointSize << "\n";
    stream << indent << "  },\n";
    stream << indent << "  \"layout\": \"" << ToString(path.layout) << "\",\n";
    stream << indent << "  \"layoutRadius\": " << path.layoutRadius << ",\n";
    stream << indent << "  \"layoutNodes\": " << path.layoutNodes << ",\n";
    stream << indent << "  \"layoutRandomness\": " << path.layoutRandomness << ",\n";
    stream << indent << "  \"controlPoints\": [\n";
    for (std::size_t index = 0; index < path.controlPoints.size(); ++index) {
        const Vec3& point = path.controlPoints[index];
        stream << indent << "    {\"x\": " << point.x << ", \"y\": " << point.y << ", \"z\": " << point.z << "}";
        stream << (index + 1 < path.controlPoints.size() ? ",\n" : "\n");
    }
    stream << indent << "  ]\n";
    stream << indent << "}";
}

void LoadPathSettings(const JsonValue& pathValue, PathSettings& path) {
    path.name = String(pathValue, "name", path.name);
    path.closed = Boolean(pathValue, "closed", path.closed);
    path.thickness = Number(pathValue, "thickness", path.thickness);
    path.taper = Number(pathValue, "taper", path.taper);
    path.twist = Number(pathValue, "twist", path.twist);
    path.repeatCount = static_cast<int>(Number(pathValue, "repeatCount", path.repeatCount));
    path.sampleCount = static_cast<int>(Number(pathValue, "sampleCount", path.sampleCount));
    if (const JsonValue* segment = pathValue.Find("segment"); segment && segment->type == JsonValue::Type::Object) {
        path.segment.mode = SegmentModeFromString(String(*segment, "mode", ToString(path.segment.mode)));
        path.segment.segments = static_cast<int>(Number(*segment, "segments", path.segment.segments));
        path.segment.sides = static_cast<int>(Number(*segment, "sides", path.segment.sides));
        path.segment.breakSides = Boolean(*segment, "breakSides", path.segment.breakSides);
        path.segment.chamfer = Boolean(*segment, "chamfer", path.segment.chamfer);
        path.segment.chamferSize = Number(*segment, "chamferSize", path.segment.chamferSize);
        path.segment.caps = Boolean(*segment, "caps", path.segment.caps);
        path.segment.size = Number(*segment, "size", path.segment.size);
        path.segment.sizeX = Number(*segment, "sizeX", path.segment.sizeX);
        path.segment.sizeY = Number(*segment, "sizeY", path.segment.sizeY);
        path.segment.sizeZ = Number(*segment, "sizeZ", path.segment.sizeZ);
        path.segment.orientToPath = Boolean(*segment, "orientToPath", path.segment.orientToPath);
        path.segment.orientReferenceAxis = PathAxisFromString(String(*segment, "orientReferenceAxis", ToString(path.segment.orientReferenceAxis)));
        path.segment.rotateX = Number(*segment, "rotateX", path.segment.rotateX);
        path.segment.rotateY = Number(*segment, "rotateY", path.segment.rotateY);
        path.segment.rotateZ = Number(*segment, "rotateZ", path.segment.rotateZ);
        path.segment.twistZ = Number(*segment, "twistZ", path.segment.twistZ);
        path.segment.debugNormals = Boolean(*segment, "debugNormals", path.segment.debugNormals);
        path.segment.tessellate = TessellationModeFromString(String(*segment, "tessellate", ToString(path.segment.tessellate)));
        path.segment.randomness = Number(*segment, "randomness", path.segment.randomness);
        // Metalheart additions - backwards compatible (defaults used if missing)
        path.segment.thicknessProfile = ThicknessProfileFromString(String(*segment, "thicknessProfile", "linear"));
        path.segment.thicknessPulseFrequency = Number(*segment, "thicknessPulseFrequency", path.segment.thicknessPulseFrequency);
        path.segment.thicknessPulseDepth = Number(*segment, "thicknessPulseDepth", path.segment.thicknessPulseDepth);
        path.segment.thicknessBlobCenter = Number(*segment, "thicknessBlobCenter", path.segment.thicknessBlobCenter);
        path.segment.thicknessBlobWidth = Number(*segment, "thicknessBlobWidth", path.segment.thicknessBlobWidth);
        path.segment.junctionSize = Number(*segment, "junctionSize", path.segment.junctionSize);
        path.segment.junctionBlend = Number(*segment, "junctionBlend", path.segment.junctionBlend);
        path.segment.tubeWarp = Number(*segment, "tubeWarp", path.segment.tubeWarp);
        path.segment.tubeWarpFrequency = Number(*segment, "tubeWarpFrequency", path.segment.tubeWarpFrequency);
        path.segment.tendrilCount = static_cast<int>(Number(*segment, "tendrilCount", path.segment.tendrilCount));
        path.segment.tendrilLength = Number(*segment, "tendrilLength", path.segment.tendrilLength);
        path.segment.tendrilThickness = Number(*segment, "tendrilThickness", path.segment.tendrilThickness);
        path.segment.tendrilWarp = Number(*segment, "tendrilWarp", path.segment.tendrilWarp);
    }
    if (const JsonValue* fractal = pathValue.Find("fractalDisplacement"); fractal && fractal->type == JsonValue::Type::Object) {
        path.fractalDisplacement.fractalType = FractalTypeFromString(String(*fractal, "fractalType", ToString(path.fractalDisplacement.fractalType)));
        path.fractalDisplacement.space = FractalSpaceFromString(String(*fractal, "space", ToString(path.fractalDisplacement.space)));
        path.fractalDisplacement.amplitude = Number(*fractal, "amplitude", path.fractalDisplacement.amplitude);
        path.fractalDisplacement.frequency = Number(*fractal, "frequency", path.fractalDisplacement.frequency);
        path.fractalDisplacement.evolution = Number(*fractal, "evolution", path.fractalDisplacement.evolution);
        path.fractalDisplacement.offsetX = Number(*fractal, "offsetX", path.fractalDisplacement.offsetX);
        path.fractalDisplacement.offsetY = Number(*fractal, "offsetY", path.fractalDisplacement.offsetY);
        path.fractalDisplacement.offsetZ = Number(*fractal, "offsetZ", path.fractalDisplacement.offsetZ);
        path.fractalDisplacement.complexity = static_cast<int>(Number(*fractal, "complexity", path.fractalDisplacement.complexity));
        path.fractalDisplacement.octScale = Number(*fractal, "octScale", path.fractalDisplacement.octScale);
        path.fractalDisplacement.octMult = Number(*fractal, "octMult", path.fractalDisplacement.octMult);
        path.fractalDisplacement.smoothenNormals = Number(*fractal, "smoothenNormals", path.fractalDisplacement.smoothenNormals);
        path.fractalDisplacement.seamlessLoop = Boolean(*fractal, "seamlessLoop", path.fractalDisplacement.seamlessLoop);
    }
    if (const JsonValue* material = pathValue.Find("material"); material && material->type == JsonValue::Type::Object) {
        path.material.renderMode = PathRenderModeFromString(String(*material, "renderMode", ToString(path.material.renderMode)));
        path.material.materialType = MaterialTypeFromString(String(*material, "materialType", ToString(path.material.materialType)));
        if (const JsonValue* primary = material->Find("primaryColor"); primary && primary->type == JsonValue::Type::Object) {
            path.material.primaryColor = ParseColor(*primary);
        }
        if (const JsonValue* accent = material->Find("accentColor"); accent && accent->type == JsonValue::Type::Object) {
            path.material.accentColor = ParseColor(*accent);
        }
        if (const JsonValue* wire = material->Find("wireColor"); wire && wire->type == JsonValue::Type::Object) {
            path.material.wireColor = ParseColor(*wire);
        }
        path.material.pointSize = Number(*material, "pointSize", path.material.pointSize);
    }
    // Metalheart: PathLayout settings - backwards compatible
    path.layout = PathLayoutFromString(String(pathValue, "layout", "user_defined"));
    path.layoutRadius = Number(pathValue, "layoutRadius", path.layoutRadius);
    path.layoutNodes = static_cast<int>(Number(pathValue, "layoutNodes", path.layoutNodes));
    path.layoutRandomness = Number(pathValue, "layoutRandomness", path.layoutRandomness);
    if (const JsonValue* points = pathValue.Find("controlPoints"); points && points->type == JsonValue::Type::Array) {
        path.controlPoints.clear();
        for (const JsonValue& pointValue : points->arrayValue) {
            path.controlPoints.push_back(ParseVec3(pointValue));
        }
    }
}

void WriteScenePose(std::ostream& stream, const ScenePose& pose, const std::string& indent) {
    stream << indent << "{\n";
    stream << indent << "  \"mode\": \"" << ToString(pose.mode) << "\",\n";
    stream << indent << "  \"gridVisible\": " << (pose.gridVisible ? "true" : "false") << ",\n";
    stream << indent << "  \"backgroundColor\": ";
    WriteColor(stream, pose.backgroundColor);
    stream << ",\n";
    stream << indent << "  \"camera\": {\n";
    WriteCameraState(stream, pose.camera, indent + "    ");
    stream << indent << "  },\n";
    stream << indent << "  \"flameRender\": {\n";
    WriteFlameRenderSettings(stream, pose.flameRender, indent + "    ");
    stream << indent << "  },\n";
    stream << indent << "  \"depthOfField\": {\n";
    WriteDepthOfFieldSettings(stream, pose.depthOfField, indent + "    ");
    stream << indent << "  },\n";
    stream << indent << "  \"denoiser\": {\n";
    WriteDenoiserSettings(stream, pose.denoiser, indent + "    ");
    stream << indent << "  },\n";
    stream << indent << "  \"postProcess\": {\n";
    WritePostProcessSettings(stream, pose.postProcess, indent + "    ");
    stream << indent << "  },\n";
    stream << indent << "  \"gradient\": [\n";
    WriteGradientStops(stream, pose.gradientStops, indent + "    ");
    stream << indent << "  ],\n";
    stream << indent << "  \"paths\": [\n";
    for (std::size_t index = 0; index < pose.paths.size(); ++index) {
        WritePathSettings(stream, pose.paths[index], indent + "    ");
        stream << (index + 1 < pose.paths.size() ? ",\n" : "\n");
    }
    stream << indent << "  ],\n";
    stream << indent << "  \"transforms\": [\n";
    WriteTransforms(stream, pose.transforms, indent + "    ");
    stream << indent << "  ]\n";
    stream << indent << "}";
}

void LoadTransforms(const JsonValue& transformsValue, std::vector<TransformLayer>& transforms) {
    if (transformsValue.type != JsonValue::Type::Array) {
        return;
    }
    transforms.clear();
    for (const JsonValue& transformValue : transformsValue.arrayValue) {
        TransformLayer layer;
        layer.name = String(transformValue, "name", "Transform");
        layer.weight = Number(transformValue, "weight", layer.weight);
        layer.rotationDegrees = Number(transformValue, "rotationDegrees", layer.rotationDegrees);
        layer.scaleX = Number(transformValue, "scaleX", layer.scaleX);
        layer.scaleY = Number(transformValue, "scaleY", layer.scaleY);
        layer.translateX = Number(transformValue, "translateX", layer.translateX);
        layer.translateY = Number(transformValue, "translateY", layer.translateY);
        layer.shearX = Number(transformValue, "shearX", layer.shearX);
        layer.shearY = Number(transformValue, "shearY", layer.shearY);
        layer.colorIndex = Number(transformValue, "colorIndex", layer.colorIndex);
        layer.useCustomColor = Boolean(transformValue, "useCustomColor", layer.useCustomColor);
        if (const JsonValue* customColor = transformValue.Find("customColor"); customColor && customColor->type == JsonValue::Type::Object) {
            layer.customColor = ParseColor(*customColor);
        }
        if (const JsonValue* variations = transformValue.Find("variations"); variations && variations->type == JsonValue::Type::Object) {
            for (std::size_t index = 0; index < kVariationCount; ++index) {
                const VariationType variation = static_cast<VariationType>(index);
                layer.variations[index] = Number(*variations, ToString(variation), layer.variations[index]);
            }
        }
        transforms.push_back(layer);
    }
}

void LoadScenePose(const JsonValue& poseValue, ScenePose& pose) {
    pose.mode = SceneModeFromString(String(poseValue, "mode", ToString(pose.mode)));
    pose.gridVisible = Boolean(poseValue, "gridVisible", pose.gridVisible);
    if (const JsonValue* backgroundColor = poseValue.Find("backgroundColor"); backgroundColor && backgroundColor->type == JsonValue::Type::Object) {
        pose.backgroundColor = ParseColor(*backgroundColor);
    }
    if (const JsonValue* camera = poseValue.Find("camera"); camera && camera->type == JsonValue::Type::Object) {
        pose.camera.yaw = Number(*camera, "yaw", pose.camera.yaw);
        pose.camera.pitch = Number(*camera, "pitch", pose.camera.pitch);
        pose.camera.distance = Number(*camera, "distance", pose.camera.distance);
        pose.camera.panX = Number(*camera, "panX", pose.camera.panX);
        pose.camera.panY = Number(*camera, "panY", pose.camera.panY);
        pose.camera.zoom2D = Number(*camera, "zoom2D", pose.camera.zoom2D);
        pose.camera.frameWidth = Number(*camera, "frameWidth", pose.camera.frameWidth);
        pose.camera.frameHeight = Number(*camera, "frameHeight", pose.camera.frameHeight);
    }
    if (const JsonValue* flameRender = poseValue.Find("flameRender"); flameRender && flameRender->type == JsonValue::Type::Object) {
        pose.flameRender.rotationXDegrees = Number(*flameRender, "rotationXDegrees", pose.flameRender.rotationXDegrees);
        pose.flameRender.rotationYDegrees = Number(*flameRender, "rotationYDegrees", pose.flameRender.rotationYDegrees);
        pose.flameRender.rotationZDegrees = Number(*flameRender, "rotationZDegrees", pose.flameRender.rotationZDegrees);
        pose.flameRender.depthAmount = Number(*flameRender, "depthAmount", pose.flameRender.depthAmount);
        pose.flameRender.symmetry = SymmetryModeFromString(String(*flameRender, "symmetry", ToString(pose.flameRender.symmetry)));
        pose.flameRender.symmetryOrder = std::clamp(static_cast<int>(Number(*flameRender, "symmetryOrder", pose.flameRender.symmetryOrder)), 2, 12);
        pose.flameRender.curveExposure = Number(*flameRender, "curveExposure", pose.flameRender.curveExposure);
        pose.flameRender.curveContrast = Number(*flameRender, "curveContrast", pose.flameRender.curveContrast);
        pose.flameRender.curveHighlights = Number(*flameRender, "curveHighlights", pose.flameRender.curveHighlights);
        pose.flameRender.curveGamma = Number(*flameRender, "curveGamma", pose.flameRender.curveGamma);
    }
    if (const JsonValue* depthOfField = poseValue.Find("depthOfField"); depthOfField && depthOfField->type == JsonValue::Type::Object) {
        pose.depthOfField.enabled = Boolean(*depthOfField, "enabled", pose.depthOfField.enabled);
        pose.depthOfField.focusDepth = Number(*depthOfField, "focusDepth", pose.depthOfField.focusDepth);
        pose.depthOfField.focusRange = Number(*depthOfField, "focusRange", pose.depthOfField.focusRange);
        pose.depthOfField.blurStrength = Number(*depthOfField, "blurStrength", pose.depthOfField.blurStrength);
    }
    if (const JsonValue* denoiser = poseValue.Find("denoiser"); denoiser && denoiser->type == JsonValue::Type::Object) {
        pose.denoiser.enabled = Boolean(*denoiser, "enabled", pose.denoiser.enabled);
        pose.denoiser.strength = Number(*denoiser, "strength", pose.denoiser.strength);
    }
    if (const JsonValue* postProcess = poseValue.Find("postProcess"); postProcess && postProcess->type == JsonValue::Type::Object) {
        pose.postProcess.enabled = Boolean(*postProcess, "enabled", pose.postProcess.enabled);
        pose.postProcess.bloomIntensity = Number(*postProcess, "bloomIntensity", pose.postProcess.bloomIntensity);
        pose.postProcess.bloomRadius = Number(*postProcess, "bloomRadius", pose.postProcess.bloomRadius);
        pose.postProcess.bloomThreshold = Number(*postProcess, "bloomThreshold", pose.postProcess.bloomThreshold);
        pose.postProcess.chromaticAberration = Number(*postProcess, "chromaticAberration", pose.postProcess.chromaticAberration);
        pose.postProcess.vignetteIntensity = Number(*postProcess, "vignetteIntensity", pose.postProcess.vignetteIntensity);
        pose.postProcess.vignetteRoundness = Number(*postProcess, "vignetteRoundness", pose.postProcess.vignetteRoundness);
        pose.postProcess.acesToneMap = Boolean(*postProcess, "acesToneMap", pose.postProcess.acesToneMap);
        pose.postProcess.filmGrain = Number(*postProcess, "filmGrain", pose.postProcess.filmGrain);
        pose.postProcess.colorTemperature = Number(*postProcess, "colorTemperature", pose.postProcess.colorTemperature);
        pose.postProcess.saturationBoost = Number(*postProcess, "saturationBoost", pose.postProcess.saturationBoost);
    }
    if (const JsonValue* gradient = poseValue.Find("gradient"); gradient && gradient->type == JsonValue::Type::Array) {
        pose.gradientStops.clear();
        for (const JsonValue& stopValue : gradient->arrayValue) {
            GradientStop stop;
            stop.position = Number(stopValue, "position", 0.0);
            stop.color = ParseColor(stopValue);
            pose.gradientStops.push_back(stop);
        }
    }
    if (const JsonValue* paths = poseValue.Find("paths"); paths && paths->type == JsonValue::Type::Array) {
        pose.paths.clear();
        for (const JsonValue& pathValue : paths->arrayValue) {
            PathSettings loadedPath = CreateDefaultScene().paths.front();
            if (pathValue.type == JsonValue::Type::Object) {
                LoadPathSettings(pathValue, loadedPath);
            }
            pose.paths.push_back(std::move(loadedPath));
        }
    }
    if (const JsonValue* transforms = poseValue.Find("transforms")) {
        LoadTransforms(*transforms, pose.transforms);
    }
}

}  // namespace

bool SceneSerializer::Save(const Scene& scene, const std::filesystem::path& path, std::string& error) const {
    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        error = "Unable to open file for writing";
        return false;
    }

    stream << "{\n";
    stream << "  \"version\": 4,\n";
    stream << "  \"name\": \"" << EscapeJson(scene.name) << "\",\n";
    stream << "  \"mode\": \"" << ToString(scene.mode) << "\",\n";
    stream << "  \"previewIterations\": " << scene.previewIterations << ",\n";
    stream << "  \"animatePath\": " << (scene.animatePath ? "true" : "false") << ",\n";
    stream << "  \"gridVisible\": " << (scene.gridVisible ? "true" : "false") << ",\n";
    stream << "  \"backgroundColor\": ";
    WriteColor(stream, scene.backgroundColor);
    stream << ",\n";
    stream << "  \"timelineFrame\": " << scene.timelineFrame << ",\n";
    stream << "  \"timelineFrameRate\": " << scene.timelineFrameRate << ",\n";
    stream << "  \"timelineStartFrame\": " << scene.timelineStartFrame << ",\n";
    stream << "  \"timelineEndFrame\": " << scene.timelineEndFrame << ",\n";
    stream << "  \"camera\": {\n";
    WriteCameraState(stream, scene.camera, "    ");
    stream << "  },\n";
    stream << "  \"flameRender\": {\n";
    WriteFlameRenderSettings(stream, scene.flameRender, "    ");
    stream << "  },\n";
    stream << "  \"depthOfField\": {\n";
    WriteDepthOfFieldSettings(stream, scene.depthOfField, "    ");
    stream << "  },\n";
    stream << "  \"denoiser\": {\n";
    WriteDenoiserSettings(stream, scene.denoiser, "    ");
    stream << "  },\n";
    stream << "  \"postProcess\": {\n";
    WritePostProcessSettings(stream, scene.postProcess, "    ");
    stream << "  },\n";
    stream << "  \"gradient\": [\n";
    WriteGradientStops(stream, scene.gradientStops, "    ");
    stream << "  ],\n";
    stream << "  \"paths\": [\n";
    for (std::size_t index = 0; index < scene.paths.size(); ++index) {
        WritePathSettings(stream, scene.paths[index], "    ");
        stream << (index + 1 < scene.paths.size() ? ",\n" : "\n");
    }
    stream << "  ],\n";
    stream << "  \"transforms\": [\n";
    WriteTransforms(stream, scene.transforms, "    ");
    stream << "  ],\n";
    stream << "  \"keyframes\": [\n";
    for (std::size_t index = 0; index < scene.keyframes.size(); ++index) {
        const SceneKeyframe& keyframe = scene.keyframes[index];
        stream << "    {\n";
        stream << "      \"frame\": " << keyframe.frame << ",\n";
        stream << "      \"markerColor\": {\"r\": " << static_cast<int>(keyframe.markerColor.r)
               << ", \"g\": " << static_cast<int>(keyframe.markerColor.g)
               << ", \"b\": " << static_cast<int>(keyframe.markerColor.b)
               << ", \"a\": " << static_cast<int>(keyframe.markerColor.a) << "},\n";
        stream << "      \"ownerType\": \"" << (keyframe.ownerType == KeyframeOwnerType::Path ? "path" : "transform") << "\",\n";
        stream << "      \"ownerIndex\": " << keyframe.ownerIndex << ",\n";
        stream << "      \"easing\": \"" << ToString(keyframe.easing) << "\",\n";
        stream << "      \"easingCurve\": {\"x1\": " << keyframe.easeX1
               << ", \"y1\": " << keyframe.easeY1
               << ", \"x2\": " << keyframe.easeX2
               << ", \"y2\": " << keyframe.easeY2 << "},\n";
        stream << "      \"pose\": ";
        WriteScenePose(stream, keyframe.pose, "");
        stream << "\n    }";
        stream << (index + 1 < scene.keyframes.size() ? ",\n" : "\n");
    }
    stream << "  ]\n";
    stream << "}\n";

    return true;
}

std::optional<Scene> SceneSerializer::Load(const std::filesystem::path& path, std::string& error) const {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = "Unable to open file";
        return std::nullopt;
    }

    std::ostringstream buffer;
    buffer << stream.rdbuf();

    JsonValue root;
    JsonParser parser(buffer.str());
    if (!parser.Parse(root, error) || root.type != JsonValue::Type::Object) {
        if (error.empty()) {
            error = "Invalid scene format";
        }
        return std::nullopt;
    }

    Scene scene = CreateDefaultScene();
    scene.name = String(root, "name", scene.name);
    scene.mode = SceneModeFromString(String(root, "mode", ToString(scene.mode)));
    scene.previewIterations = static_cast<std::uint32_t>(Number(root, "previewIterations", scene.previewIterations));
    scene.animatePath = Boolean(root, "animatePath", scene.animatePath);
    scene.gridVisible = Boolean(root, "gridVisible", scene.gridVisible);
    scene.timelineFrameRate = Number(root, "timelineFrameRate", scene.timelineFrameRate);
    scene.timelineStartFrame = static_cast<int>(Number(root, "timelineStartFrame", scene.timelineStartFrame));
    scene.timelineEndFrame = static_cast<int>(Number(root, "timelineEndFrame", scene.timelineEndFrame));
    scene.timelineFrame = Number(root, "timelineFrame", scene.timelineFrame);
    scene.timelineSeconds = Number(root, "timelineSeconds", scene.timelineSeconds);
    if (const JsonValue* backgroundColor = root.Find("backgroundColor"); backgroundColor && backgroundColor->type == JsonValue::Type::Object) {
        scene.backgroundColor = ParseColor(*backgroundColor);
    }

    if (const JsonValue* camera = root.Find("camera"); camera && camera->type == JsonValue::Type::Object) {
        scene.camera.yaw = Number(*camera, "yaw", scene.camera.yaw);
        scene.camera.pitch = Number(*camera, "pitch", scene.camera.pitch);
        scene.camera.distance = Number(*camera, "distance", scene.camera.distance);
        scene.camera.panX = Number(*camera, "panX", scene.camera.panX);
        scene.camera.panY = Number(*camera, "panY", scene.camera.panY);
        scene.camera.zoom2D = Number(*camera, "zoom2D", scene.camera.zoom2D);
        scene.camera.frameWidth = Number(*camera, "frameWidth", scene.camera.frameWidth);
        scene.camera.frameHeight = Number(*camera, "frameHeight", scene.camera.frameHeight);
    }
    if (const JsonValue* flameRender = root.Find("flameRender"); flameRender && flameRender->type == JsonValue::Type::Object) {
        scene.flameRender.rotationXDegrees = Number(*flameRender, "rotationXDegrees", scene.flameRender.rotationXDegrees);
        scene.flameRender.rotationYDegrees = Number(*flameRender, "rotationYDegrees", scene.flameRender.rotationYDegrees);
        scene.flameRender.rotationZDegrees = Number(*flameRender, "rotationZDegrees", scene.flameRender.rotationZDegrees);
        scene.flameRender.depthAmount = Number(*flameRender, "depthAmount", scene.flameRender.depthAmount);
        scene.flameRender.symmetry = SymmetryModeFromString(String(*flameRender, "symmetry", ToString(scene.flameRender.symmetry)));
        scene.flameRender.symmetryOrder = std::clamp(static_cast<int>(Number(*flameRender, "symmetryOrder", scene.flameRender.symmetryOrder)), 2, 12);
        scene.flameRender.curveExposure = Number(*flameRender, "curveExposure", scene.flameRender.curveExposure);
        scene.flameRender.curveContrast = Number(*flameRender, "curveContrast", scene.flameRender.curveContrast);
        scene.flameRender.curveHighlights = Number(*flameRender, "curveHighlights", scene.flameRender.curveHighlights);
        scene.flameRender.curveGamma = Number(*flameRender, "curveGamma", scene.flameRender.curveGamma);
    }
    if (const JsonValue* depthOfField = root.Find("depthOfField"); depthOfField && depthOfField->type == JsonValue::Type::Object) {
        scene.depthOfField.enabled = Boolean(*depthOfField, "enabled", scene.depthOfField.enabled);
        scene.depthOfField.focusDepth = Number(*depthOfField, "focusDepth", scene.depthOfField.focusDepth);
        scene.depthOfField.focusRange = Number(*depthOfField, "focusRange", scene.depthOfField.focusRange);
        scene.depthOfField.blurStrength = Number(*depthOfField, "blurStrength", scene.depthOfField.blurStrength);
    }
    if (const JsonValue* denoiser = root.Find("denoiser"); denoiser && denoiser->type == JsonValue::Type::Object) {
        scene.denoiser.enabled = Boolean(*denoiser, "enabled", scene.denoiser.enabled);
        scene.denoiser.strength = Number(*denoiser, "strength", scene.denoiser.strength);
    }
    if (const JsonValue* postProcess = root.Find("postProcess"); postProcess && postProcess->type == JsonValue::Type::Object) {
        scene.postProcess.enabled = Boolean(*postProcess, "enabled", scene.postProcess.enabled);
        scene.postProcess.bloomIntensity = Number(*postProcess, "bloomIntensity", scene.postProcess.bloomIntensity);
        scene.postProcess.bloomRadius = Number(*postProcess, "bloomRadius", scene.postProcess.bloomRadius);
        scene.postProcess.bloomThreshold = Number(*postProcess, "bloomThreshold", scene.postProcess.bloomThreshold);
        scene.postProcess.chromaticAberration = Number(*postProcess, "chromaticAberration", scene.postProcess.chromaticAberration);
        scene.postProcess.vignetteIntensity = Number(*postProcess, "vignetteIntensity", scene.postProcess.vignetteIntensity);
        scene.postProcess.vignetteRoundness = Number(*postProcess, "vignetteRoundness", scene.postProcess.vignetteRoundness);
        scene.postProcess.acesToneMap = Boolean(*postProcess, "acesToneMap", scene.postProcess.acesToneMap);
        scene.postProcess.filmGrain = Number(*postProcess, "filmGrain", scene.postProcess.filmGrain);
        scene.postProcess.colorTemperature = Number(*postProcess, "colorTemperature", scene.postProcess.colorTemperature);
        scene.postProcess.saturationBoost = Number(*postProcess, "saturationBoost", scene.postProcess.saturationBoost);
    }
    if (const JsonValue* gradient = root.Find("gradient"); gradient && gradient->type == JsonValue::Type::Array) {
        scene.gradientStops.clear();
        for (const JsonValue& stopValue : gradient->arrayValue) {
            GradientStop stop;
            stop.position = Number(stopValue, "position", 0.0);
            stop.color = ParseColor(stopValue);
            scene.gradientStops.push_back(stop);
        }
    }

    if (const JsonValue* paths = root.Find("paths"); paths && paths->type == JsonValue::Type::Array) {
        scene.paths.clear();
        for (const JsonValue& pathValue : paths->arrayValue) {
            PathSettings loadedPath = CreateDefaultScene().paths.front();
            if (pathValue.type == JsonValue::Type::Object) {
                LoadPathSettings(pathValue, loadedPath);
            }
            scene.paths.push_back(std::move(loadedPath));
        }
    } else if (const JsonValue* pathValue = root.Find("path"); pathValue && pathValue->type == JsonValue::Type::Object) {
        scene.paths = {CreateDefaultScene().paths.front()};
        LoadPathSettings(*pathValue, scene.paths.front());
    }

    if (const JsonValue* transforms = root.Find("transforms")) {
        LoadTransforms(*transforms, scene.transforms);
    }

    if (const JsonValue* keyframes = root.Find("keyframes"); keyframes && keyframes->type == JsonValue::Type::Array) {
        scene.keyframes.clear();
        for (const JsonValue& keyframeValue : keyframes->arrayValue) {
            SceneKeyframe keyframe;
            keyframe.frame = static_cast<int>(Number(keyframeValue, "frame", 0.0));
            if (const JsonValue* markerColor = keyframeValue.Find("markerColor"); markerColor && markerColor->type == JsonValue::Type::Object) {
                keyframe.markerColor = ParseColor(*markerColor);
            }
            const std::string ownerType = String(keyframeValue, "ownerType", "");
            if (ownerType == "path") {
                keyframe.ownerType = KeyframeOwnerType::Path;
            } else if (ownerType == "transform") {
                keyframe.ownerType = KeyframeOwnerType::Transform;
            } else {
                keyframe.ownerType = keyframe.markerColor.b > keyframe.markerColor.r ? KeyframeOwnerType::Path : KeyframeOwnerType::Transform;
            }
            keyframe.ownerIndex = static_cast<int>(Number(keyframeValue, "ownerIndex", 0.0));
            keyframe.easing = KeyframeEasingFromString(String(keyframeValue, "easing", ToString(keyframe.easing)));
            ApplyKeyframeEasingPreset(keyframe, keyframe.easing);
            if (const JsonValue* easingCurve = keyframeValue.Find("easingCurve"); easingCurve && easingCurve->type == JsonValue::Type::Object) {
                keyframe.easeX1 = Number(*easingCurve, "x1", keyframe.easeX1);
                keyframe.easeY1 = Number(*easingCurve, "y1", keyframe.easeY1);
                keyframe.easeX2 = Number(*easingCurve, "x2", keyframe.easeX2);
                keyframe.easeY2 = Number(*easingCurve, "y2", keyframe.easeY2);
            }
            if (const JsonValue* pose = keyframeValue.Find("pose"); pose && pose->type == JsonValue::Type::Object) {
                keyframe.pose = CaptureScenePose(scene);
                LoadScenePose(*pose, keyframe.pose);
            }
            scene.keyframes.push_back(std::move(keyframe));
        }
        SortKeyframes(scene);
    }

    if (root.Find("timelineFrame") == nullptr && root.Find("timelineSeconds") != nullptr) {
        scene.timelineFrame = std::round(scene.timelineSeconds * std::max(1.0, scene.timelineFrameRate));
    }
    scene.timelineFrame = Clamp(scene.timelineFrame, static_cast<double>(scene.timelineStartFrame), static_cast<double>(std::max(scene.timelineStartFrame, scene.timelineEndFrame)));
    scene.timelineSeconds = TimelineSecondsForFrame(scene, scene.timelineFrame);

    return scene;
}

}  // namespace radiary
