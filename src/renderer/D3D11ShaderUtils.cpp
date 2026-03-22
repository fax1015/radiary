#include "renderer/D3D11ShaderUtils.h"

#include <windows.h>

#include <cstdio>
#include <filesystem>

namespace radiary {

namespace {

std::filesystem::path ResolveShaderPath(const wchar_t* shaderFilename) {
    wchar_t modulePath[MAX_PATH] {};
    const DWORD length = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    std::filesystem::path executablePath;
    if (length > 0) {
        executablePath = std::filesystem::path(modulePath).parent_path();
    }

#ifdef RADIARY_SHADER_DIR
    const std::filesystem::path shaderDirectory = RADIARY_SHADER_DIR;
#else
    const std::filesystem::path shaderDirectory = L"shaders";
#endif

    return executablePath / shaderDirectory / shaderFilename;
}

}  // namespace

UINT GetOptimizedD3D11ShaderCompileFlags(const bool enableStrictness, const bool preferFlowControl) {
    UINT compileFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
    if (enableStrictness) {
        compileFlags |= D3DCOMPILE_ENABLE_STRICTNESS;
    }
    if (preferFlowControl) {
        compileFlags |= D3DCOMPILE_PREFER_FLOW_CONTROL;
    }
    return compileFlags;
}

UINT GetDevelopmentD3D11ShaderCompileFlags(const bool enableStrictness, const bool preferFlowControl) {
#if defined(_DEBUG)
    UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
    if (enableStrictness) {
        compileFlags |= D3DCOMPILE_ENABLE_STRICTNESS;
    }
    if (preferFlowControl) {
        compileFlags |= D3DCOMPILE_PREFER_FLOW_CONTROL;
    }
    return compileFlags;
#else
    return GetOptimizedD3D11ShaderCompileFlags(enableStrictness, preferFlowControl);
#endif
}

ID3DBlob* CompileD3D11ShaderFromFile(
    const wchar_t* shaderFilename,
    const char* entryPoint,
    const char* target,
    const UINT compileFlags,
    std::string& error) {
    const std::filesystem::path shaderPath = ResolveShaderPath(shaderFilename);
    ID3DBlob* shaderBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;
    const HRESULT result = D3DCompileFromFile(
        shaderPath.c_str(),
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entryPoint,
        target,
        compileFlags,
        0,
        &shaderBlob,
        &errorBlob);

    if (errorBlob != nullptr) {
        error.assign(static_cast<const char*>(errorBlob->GetBufferPointer()), errorBlob->GetBufferSize());
        errorBlob->Release();
    } else {
        error.clear();
    }

    if (FAILED(result)) {
        if (shaderBlob != nullptr) {
            shaderBlob->Release();
        }
        if (error.empty()) {
            char buffer[512] {};
            std::snprintf(
                buffer,
                sizeof(buffer),
                "D3DCompileFromFile(%ls, %s) failed (HRESULT 0x%08X)",
                shaderPath.c_str(),
                entryPoint,
                static_cast<unsigned int>(result));
            error = buffer;
        }
        return nullptr;
    }

    return shaderBlob;
}

bool CreateD3D11ComputeShaderFromFile(
    ID3D11Device* device,
    const wchar_t* shaderFilename,
    const char* entryPoint,
    const UINT compileFlags,
    const char* createStageName,
    ID3D11ComputeShader** shader,
    std::string& error) {
    if (shader == nullptr) {
        error = "CreateD3D11ComputeShaderFromFile received a null shader output pointer.";
        return false;
    }

    *shader = nullptr;
    ID3DBlob* shaderBlob = CompileD3D11ShaderFromFile(shaderFilename, entryPoint, "cs_5_0", compileFlags, error);
    if (shaderBlob == nullptr) {
        return false;
    }

    const HRESULT result = device->CreateComputeShader(
        shaderBlob->GetBufferPointer(),
        shaderBlob->GetBufferSize(),
        nullptr,
        shader);
    shaderBlob->Release();
    if (FAILED(result)) {
        error = FormatD3D11StageError(createStageName, result);
        return false;
    }

    error.clear();
    return true;
}

std::string FormatD3D11StageError(const char* stage, const HRESULT result) {
    char buffer[128] {};
    std::snprintf(buffer, sizeof(buffer), "%s failed (HRESULT 0x%08X)", stage, static_cast<unsigned int>(result));
    return buffer;
}

}  // namespace radiary
