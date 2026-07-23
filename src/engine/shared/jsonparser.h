/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
#ifndef ENGINE_SHARED_JSONPARSER_H
#define ENGINE_SHARED_JSONPARSER_H

#include <engine/external/json-parser/json.h>

class CJsonParser
{
	char m_aError[256];
	json_value *m_pParsedJson;

public:
	CJsonParser();
	~CJsonParser();
	json_value *ParseData(const void *pFileData, unsigned FileSize, const char *pContext = "rawdata");
	json_value *ParseString(const char *pString, const char *pContext = "string");
	json_value *ParsedJson() { return m_pParsedJson; }
	const char *Error() const { return m_aError; }
};

#endif
