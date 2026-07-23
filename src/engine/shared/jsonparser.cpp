#include "jsonparser.h"
#include <base/system.h>

CJsonParser::CJsonParser() : m_pParsedJson(0)
{
	m_aError[0] = 0;
}

CJsonParser::~CJsonParser()
{
	if(m_pParsedJson)
		json_value_free(m_pParsedJson);
}

json_value *CJsonParser::ParseData(const void *pFileData, unsigned FileSize, const char *pContext)
{
	if(m_pParsedJson)
	{
		json_value_free(m_pParsedJson);
		m_pParsedJson = 0;
	}
	m_aError[0] = 0;

	json_settings JsonSettings;
	mem_zero(&JsonSettings, sizeof(JsonSettings));
	JsonSettings.settings |= json_enable_comments;
	char aJsonError[json_error_max];
	m_pParsedJson = json_parse_ex(&JsonSettings, static_cast<const json_char *>(pFileData), FileSize, aJsonError);

	if(!m_pParsedJson)
		str_format(m_aError, sizeof(m_aError), "Failed to parse '%s': %s", pContext, aJsonError);

	return m_pParsedJson;
}

json_value *CJsonParser::ParseString(const char *pString, const char *pContext)
{
	return ParseData(pString, str_length(pString), pContext);
}
