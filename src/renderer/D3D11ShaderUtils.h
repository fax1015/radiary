#pragma once

#include <d3d11.h>
#include <d3dcompiler.h>

#include <string>

namespace radiary {

UINT GetOptimizedD3D11ShaderCompileFlags(bool enableStrictness = false, bool preferFlowControl = false);
UINT GetDevelopmentD3D11ShaderCompileFlags(bool enableStrictness = false, bool preferFlowControl = false);

ID3DBlob* CompileD3D11ShaderFromFile(
    const wchar_t* shaderFilename,
    const char* entryPoint,
    const char* target,
    UINT compileFlags,
    std::string& error);

bool CreateD3D11ComputeShaderFromFile(
    ID3D11Device* device,
    const wchar_t* shaderFilename,
    const char* entryPoint,
    UINT compileFlags,
    const char* createStageName,
    ID3D11ComputeShader** shader,
    std::string& error);

std::string FormatD3D11StageError(const char* stage, HRESULT result);

}  // namespace radiary
