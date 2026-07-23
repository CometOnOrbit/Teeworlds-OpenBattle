#include <base/math.h>
#include <base/system.h>
#include <base/tl/stream.h>
#include <engine/engine.h>
#include <game/version.h>

#include "http_request.h"

#include <curl/curl.h>

#if defined(CONF_FAMILY_UNIX)
#include <signal.h>
#endif

CCurlInit::CCurlInit()
{
#if defined(CONF_FAMILY_UNIX)
	signal(SIGPIPE, SIG_IGN);
#endif
	m_Failed = curl_global_init(CURL_GLOBAL_ALL) != 0;
	if(m_Failed)
		dbg_msg("http", "failed to init curl");
}

CCurlInit::~CCurlInit()
{
	curl_global_cleanup();
}

void EscapeUrl(char *pBuf, int Size, const char *pStr)
{
	char *pEsc = curl_easy_escape(0, pStr, 0);
	str_copy(pBuf, pEsc, Size);
	curl_free(pEsc);
}

CHttpRequest::CHttpRequest(const char *pRequest, const char *pUrl, long TimeoutSeconds, int IPResolve)
{
	str_copy(m_aRequest, pRequest, sizeof(m_aRequest));
	str_copy(m_aUrl, pUrl, sizeof(m_aUrl));
	m_pHeaderList = 0;
	m_IPResolve = IPResolve;
	m_IsChecked = false;
	m_TimeoutSeconds = TimeoutSeconds;
	m_PostData.clear();
	m_CurlResult = 0;
	m_ResponseCode = 0;
	m_pHandle = 0;
}

void CHttpRequest::PostData(const unsigned char *pPost, int Size)
{
	memory_stream<unsigned char> Stream(&m_PostData);
	Stream.write(pPost, Size);
}

void CHttpRequest::PostJson(const char *pJson)
{
	PostData((const unsigned char *)pJson, str_length(pJson));
	AddHeader("Content-Type: application/json");
}

void CHttpRequest::AddHeader(const char *pHeader)
{
	m_pHeaderList = curl_slist_append(m_pHeaderList, pHeader);
}

#ifdef AddJob
#undef AddJob
#endif

void CHttpRequest::StartRun(IEngine *pEngine)
{
	pEngine->AddJob(&m_Job, CHttpRequest::Run, this);
}

void CHttpRequest::StartRunBlocking()
{
	// ponytail: no http_thread — run curl in the calling (job) thread
	m_CurlResult = RunSync(this);
}

int CHttpRequest::Result() const
{
	return m_CurlResult != 0 ? -1 : 0;
}

size_t CHttpRequest::WriteCallback(char *pData, size_t Size, size_t Number, void *pUser)
{
	CHttpRequest *pRequest = static_cast<CHttpRequest *>(pUser);
	size_t TotalSize = Size * Number;
	memory_stream<unsigned char> Stream(&pRequest->m_ReceivedData);
	Stream.write((const unsigned char *)pData, TotalSize);
	return TotalSize;
}

bool CHttpRequest::ConfigureEasy(void *pHandle)
{
	CURL *pEasy = (CURL *)pHandle;
	curl_easy_setopt(pEasy, CURLOPT_URL, m_aUrl);
	curl_easy_setopt(pEasy, CURLOPT_CUSTOMREQUEST, m_aRequest);
#if LIBCURL_VERSION_NUM >= 0x075500
	curl_easy_setopt(pEasy, CURLOPT_PROTOCOLS_STR, "https");
#else
	curl_easy_setopt(pEasy, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
#endif
	curl_easy_setopt(pEasy, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(pEasy, CURLOPT_MAXREDIRS, 5L);
	curl_easy_setopt(pEasy, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(pEasy, CURLOPT_CONNECTTIMEOUT, 5L);
	curl_easy_setopt(pEasy, CURLOPT_TIMEOUT, m_TimeoutSeconds);
	curl_easy_setopt(pEasy, CURLOPT_POSTFIELDS, (const char *)m_PostData.base_ptr());
	curl_easy_setopt(pEasy, CURLOPT_POSTFIELDSIZE, max(0, m_PostData.size()));
	switch(m_IPResolve)
	{
	case HTTP_IPRESOLVE_IPV4ONLY: curl_easy_setopt(pEasy, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4); break;
	case HTTP_IPRESOLVE_IPV6ONLY: curl_easy_setopt(pEasy, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V6); break;
	default: curl_easy_setopt(pEasy, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_WHATEVER); break;
	}
	curl_easy_setopt(pEasy, CURLOPT_USERAGENT, "OpenBattle " GAME_VERSION " (" CONF_PLATFORM_STRING "; " CONF_ARCH_STRING ")");
	curl_easy_setopt(pEasy, CURLOPT_HTTPHEADER, m_pHeaderList);
	curl_easy_setopt(pEasy, CURLOPT_ACCEPT_ENCODING, "");
	curl_easy_setopt(pEasy, CURLOPT_WRITEFUNCTION, CHttpRequest::WriteCallback);
	curl_easy_setopt(pEasy, CURLOPT_WRITEDATA, this);
	return true;
}

void CHttpRequest::CompleteRequest(int CurlResult, void *pEasyHandle)
{
	m_CurlResult = CurlResult;
	if(CurlResult != CURLE_OK)
		dbg_msg("http", "libcurl error (%d): %s", CurlResult, curl_easy_strerror((CURLcode)CurlResult));
	m_ResponseCode = 0;
	void *pInfo = pEasyHandle ? pEasyHandle : m_pHandle;
	if(pInfo)
	{
		long ResponseCode = 0;
		curl_easy_getinfo((CURL *)pInfo, CURLINFO_RESPONSE_CODE, &ResponseCode);
		m_ResponseCode = (int)ResponseCode;
	}
	curl_slist_free_all(m_pHeaderList);
	m_pHeaderList = 0;
}

int CHttpRequest::RunSync(void *pUser)
{
	CHttpRequest *pRequest = static_cast<CHttpRequest *>(pUser);
	pRequest->m_ReceivedData.clear();
	pRequest->m_pHandle = curl_easy_init();
	if(!pRequest->m_pHandle)
		return -1;
	if(!pRequest->ConfigureEasy(pRequest->m_pHandle))
	{
		curl_easy_cleanup(pRequest->m_pHandle);
		pRequest->m_pHandle = 0;
		return -1;
	}
	CURLcode Result = curl_easy_perform((CURL *)pRequest->m_pHandle);
	pRequest->CompleteRequest(Result, pRequest->m_pHandle);
	curl_easy_cleanup((CURL *)pRequest->m_pHandle);
	pRequest->m_pHandle = 0;
	return Result != CURLE_OK ? -1 : 0;
}

int CHttpRequest::Run(void *pUser)
{
	return RunSync(pUser);
}
