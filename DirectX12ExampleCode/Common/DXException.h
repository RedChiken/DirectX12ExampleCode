#pragma once
#include <Windows.h>
#include <string>

class DXException
{
public:
	DXException();
	DXException(HRESULT hr, const std::wstring& functionName, const std::wstring& filename, int lineNumber);

	std::wstring ToString() const;

	HRESULT ErrorCode = S_OK;
	std::wstring FunctionName;
	std::wstring FileName;
	int LineNumber = -1;
};