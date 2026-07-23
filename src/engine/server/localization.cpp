// From Ninslash
#include "localization.h"
#include <engine/localization.h>

CLocalization::CLocalization(IStorage *pStorage)
{
    m_pStorage = pStorage;
}

void CLocalization::Init()
{
	// ponytail: sync load — OpenBattle has no thread_init; files are tiny
	LoadLocalizations(this);
}

void CLocalization::LoadLocalizations(void *pUser)
{
	CLocalization *pThis = (CLocalization *)pUser;

    const char *pIndex = "./data/languages/index.json";
    IOHANDLE File = pThis->m_pStorage->OpenFile(pIndex, IOFLAG_READ, IStorage::TYPE_ALL);
	if(!File)
	{
		dbg_msg("Localization", "can't open ./data/languages/index.json");
		return;
	}

    const int FileSize = (int)io_length(File);
	char* pFileData = (char*)malloc(FileSize);
	io_read(File, pFileData, FileSize);
	io_close(File);

    // parse json data
    json_settings JsonSettings;
    mem_zero(&JsonSettings, sizeof(JsonSettings));
    char aError[256];
    json_value *pJsonData = json_parse_ex(&JsonSettings, pFileData, FileSize, aError);
	free(pFileData);
    if(pJsonData == nullptr)
	{
		dbg_msg("Localization", "Can't load the localization file %s : %s", pIndex, aError);
		return;
	}

    const json_value &rStart = (*pJsonData)["language indices"];

    if (rStart.type == json_array)
    {
		// Set i = 1, Skip English
        for (unsigned i = 1; i < rStart.u.array.length; ++i)
        {
			if(!pThis->LoadLanguage(rStart[i]["file"]))
				dbg_msg("Localization", "Can't load the localization file %s", (const char *)rStart[i]["file"]);
		}
    }

    // clean up
    json_value_free(pJsonData);

	dbg_msg("Localization", "Localization loaded");
}

bool CLocalization::LoadLanguage(const char *pFile)
{
    char aFilePath[64];
    str_format(aFilePath, sizeof(aFilePath), "./data/languages/%s.json", pFile);
    IOHANDLE File = m_pStorage->OpenFile(aFilePath, IOFLAG_READ, IStorage::TYPE_ALL);
	if(!File)
		return false;
    int FileSize = (int)io_length(File);
    char *pFileData = new char[FileSize + 1];
    io_read(File, pFileData, FileSize);
    pFileData[FileSize] = 0;
    io_close(File);

    // parse json data
    json_settings JsonSettings;
    mem_zero(&JsonSettings, sizeof(JsonSettings));
    char aError[256];
    json_value *pJsonData = json_parse_ex(&JsonSettings, pFileData, FileSize, aError);
    if(pJsonData == 0)
	{
		dbg_msg("Localization", "Can't load the localization file %s : %s", aFilePath, aError);
		delete[] pFileData;
		return false;
	}

    const json_value &rStart = (*pJsonData)["translation"];

    if (rStart.type == json_array)
    {
		str_copy(m_aLocalize[pFile].m_aLanguageName, pFile, sizeof(m_aLocalize[pFile].m_aLanguageName));
        for (unsigned i = 0; i < rStart.u.array.length; ++i)
			AddNewLocalize(pFile, rStart[i]["key"], rStart[i]["value"]);
	}

    // clean up
    json_value_free(pJsonData);
    delete[] pFileData;

    return true;
}

void CLocalization::AddNewLocalize(const char *pName, const char *pKey, const char *pValue)
{
	m_aLocalize[pName].m_aLocalizedTexts[pKey] = pValue;
}

const char *CLocalization::GetLanguageCode(int Country)
{
	// Constants from 'data/countryflags/index.txt'
	switch(Country)
	{
		/* ar - Arabic ************************************/
		case 12: //Algeria
		case 48: //Bahrain
		case 262: //Djibouti
		case 818: //Egypt
		case 368: //Iraq
		case 400: //Jordan
		case 414: //Kuwait
		case 422: //Lebanon
		case 434: //Libya
		case 478: //Mauritania
		case 504: //Morocco
		case 512: //Oman
		case 275: //Palestine
		case 634: //Qatar
		case 682: //Saudi Arabia
		case 706: //Somalia
		case 729: //Sudan
		case 760: //Syria
		case 788: //Tunisia
		case 784: //United Arab Emirates
		case 887: //Yemen
			return "ar";
		/* bg - Bosnian *************************************/
		case 100: //Bulgaria
			return "bg";
		/* bs - Bosnian *************************************/
		case 70: //Bosnia and Hercegovina
			return "bs";
		/* cs - Czech *************************************/
		case 203: //Czechia
			return "cs";
		/* de - German ************************************/
		case 40: //Austria
		case 276: //Germany
		case 438: //Liechtenstein
		case 756: //Switzerland
			return "de";
		/* el - Greek ***********************************/
		case 300: //Greece
		case 196: //Cyprus
			return "el";
		/* es - Spanish ***********************************/
		case 32: //Argentina
		case 68: //Bolivia
		case 152: //Chile
		case 170: //Colombia
		case 188: //Costa Rica
		case 192: //Cuba
		case 214: //Dominican Republic
		case 218: //Ecuador
		case 222: //El Salvador
		case 226: //Equatorial Guinea
		case 320: //Guatemala
		case 340: //Honduras
		case 484: //Mexico
		case 558: //Nicaragua
		case 591: //Panama
		case 600: //Paraguay
		case 604: //Peru
		case 630: //Puerto Rico
		case 724: //Spain
		case 858: //Uruguay
		case 862: //Venezuela
			return "es";
		/* fa - Farsi ************************************/
		case 364: //Islamic Republic of Iran
		case 4: //Afghanistan
			return "fa";
		/* fr - French ************************************/
		case 204: //Benin
		case 854: //Burkina Faso
		case 178: //Republic of the Congo
		case 384: //Cote d’Ivoire
		case 266: //Gabon
		case 324: //Ginea
		case 466: //Mali
		case 562: //Niger
		case 686: //Senegal
		case 768: //Togo
		case 250: //France
		case 492: //Monaco
			return "fr";
		/* hr - Croatian **********************************/
		case 191: //Croatia
			return "hr";
		/* hu - Hungarian *********************************/
		case 348: //Hungary
			return "hu";
		/* it - Italian ***********************************/
		case 380: //Italy
			return "it";
		/* ja - Japanese **********************************/
		case 392: //Japan
			return "ja";
		/* la - Latin *************************************/
		case 336: //Vatican
			return "la";
		/* nl - Dutch *************************************/
		case 533: //Aruba
		case 531: //Curaçao
		case 534: //Sint Maarten
		case 528: //Netherland
		case 740: //Suriname
		case 56: //Belgique
			return "nl";
		/* pl - Polish *************************************/
		case 616: //Poland
			return "pl";
		/* pt - Portuguese ********************************/
		case 24: //Angola
		case 76: //Brazil
		case 132: //Cape Verde
		//case 226: //Equatorial Guinea: official language, but not national language
		//case 446: //Macao: official language, but spoken by less than 1% of the population
		case 508: //Mozambique
		case 626: //Timor-Leste
		case 678: //São Tomé and Príncipe
			return "pt";
		/* ru - Russian ***********************************/
		case 112: //Belarus
		case 643: //Russia
		case 398: //Kazakhstan
			return "ru";
		/* sk - Slovak ************************************/
		case 703: //Slovakia
			return "sk";
		/* sr - Serbian ************************************/
		case 688: //Serbia
			return "sr";
		/* tl - Tagalog ************************************/
		case 608: //Philippines
			return "tl";
		/* tr - Turkish ************************************/
		case 31: //Azerbaijan
		case 792: //Turkey
			return "tr";
		/* uk - Ukrainian **********************************/
		case 804: //Ukraine
			return "uk";
		/* zh-cn - Chinese (Simplified) **********************************/
		case 156: //People’s Republic of China
			return "zh-cn";
		/* zh-hk - Chinese (Traditional) **********************************/
		case 158: //Taiwan
		case 344: //Hong Kong
		case 446: //Macau
			return "zh-hk";
		case 826: // United Kingdom of Great Britain and Northern Ireland
		case 840: // United States of America
			return "en";
		default:
			return "en";
	}
}

static int IsoAlpha2ToCountryNum(const char *pIso)
{
	if(!pIso || str_length(pIso) < 2)
		return -1;
	char a[3];
	a[0] = str_uppercase(pIso[0]);
	a[1] = str_uppercase(pIso[1]);
	a[2] = 0;

	// Same countries as GetLanguageCode (ISO 3166-1 alpha-2 → numeric)
	if(!str_comp(a, "DZ")) return 12;
	if(!str_comp(a, "BH")) return 48;
	if(!str_comp(a, "DJ")) return 262;
	if(!str_comp(a, "EG")) return 818;
	if(!str_comp(a, "IQ")) return 368;
	if(!str_comp(a, "JO")) return 400;
	if(!str_comp(a, "KW")) return 414;
	if(!str_comp(a, "LB")) return 422;
	if(!str_comp(a, "LY")) return 434;
	if(!str_comp(a, "MR")) return 478;
	if(!str_comp(a, "MA")) return 504;
	if(!str_comp(a, "OM")) return 512;
	if(!str_comp(a, "PS")) return 275;
	if(!str_comp(a, "QA")) return 634;
	if(!str_comp(a, "SA")) return 682;
	if(!str_comp(a, "SO")) return 706;
	if(!str_comp(a, "SD")) return 729;
	if(!str_comp(a, "SY")) return 760;
	if(!str_comp(a, "TN")) return 788;
	if(!str_comp(a, "AE")) return 784;
	if(!str_comp(a, "YE")) return 887;
	if(!str_comp(a, "BG")) return 100;
	if(!str_comp(a, "BA")) return 70;
	if(!str_comp(a, "CZ")) return 203;
	if(!str_comp(a, "AT")) return 40;
	if(!str_comp(a, "DE")) return 276;
	if(!str_comp(a, "LI")) return 438;
	if(!str_comp(a, "CH")) return 756;
	if(!str_comp(a, "GR")) return 300;
	if(!str_comp(a, "CY")) return 196;
	if(!str_comp(a, "AR")) return 32;
	if(!str_comp(a, "BO")) return 68;
	if(!str_comp(a, "CL")) return 152;
	if(!str_comp(a, "CO")) return 170;
	if(!str_comp(a, "CR")) return 188;
	if(!str_comp(a, "CU")) return 192;
	if(!str_comp(a, "DO")) return 214;
	if(!str_comp(a, "EC")) return 218;
	if(!str_comp(a, "SV")) return 222;
	if(!str_comp(a, "GQ")) return 226;
	if(!str_comp(a, "GT")) return 320;
	if(!str_comp(a, "HN")) return 340;
	if(!str_comp(a, "MX")) return 484;
	if(!str_comp(a, "NI")) return 558;
	if(!str_comp(a, "PA")) return 591;
	if(!str_comp(a, "PY")) return 600;
	if(!str_comp(a, "PE")) return 604;
	if(!str_comp(a, "PR")) return 630;
	if(!str_comp(a, "ES")) return 724;
	if(!str_comp(a, "UY")) return 858;
	if(!str_comp(a, "VE")) return 862;
	if(!str_comp(a, "IR")) return 364;
	if(!str_comp(a, "AF")) return 4;
	if(!str_comp(a, "BJ")) return 204;
	if(!str_comp(a, "BF")) return 854;
	if(!str_comp(a, "CG")) return 178;
	if(!str_comp(a, "CI")) return 384;
	if(!str_comp(a, "GA")) return 266;
	if(!str_comp(a, "GN")) return 324;
	if(!str_comp(a, "ML")) return 466;
	if(!str_comp(a, "NE")) return 562;
	if(!str_comp(a, "SN")) return 686;
	if(!str_comp(a, "TG")) return 768;
	if(!str_comp(a, "FR")) return 250;
	if(!str_comp(a, "MC")) return 492;
	if(!str_comp(a, "HR")) return 191;
	if(!str_comp(a, "HU")) return 348;
	if(!str_comp(a, "IT")) return 380;
	if(!str_comp(a, "JP")) return 392;
	if(!str_comp(a, "VA")) return 336;
	if(!str_comp(a, "AW")) return 533;
	if(!str_comp(a, "CW")) return 531;
	if(!str_comp(a, "SX")) return 534;
	if(!str_comp(a, "NL")) return 528;
	if(!str_comp(a, "SR")) return 740;
	if(!str_comp(a, "BE")) return 56;
	if(!str_comp(a, "PL")) return 616;
	if(!str_comp(a, "AO")) return 24;
	if(!str_comp(a, "BR")) return 76;
	if(!str_comp(a, "CV")) return 132;
	if(!str_comp(a, "MZ")) return 508;
	if(!str_comp(a, "TL")) return 626;
	if(!str_comp(a, "ST")) return 678;
	if(!str_comp(a, "BY")) return 112;
	if(!str_comp(a, "RU")) return 643;
	if(!str_comp(a, "KZ")) return 398;
	if(!str_comp(a, "SK")) return 703;
	if(!str_comp(a, "RS")) return 688;
	if(!str_comp(a, "PH")) return 608;
	if(!str_comp(a, "AZ")) return 31;
	if(!str_comp(a, "TR")) return 792;
	if(!str_comp(a, "UA")) return 804;
	if(!str_comp(a, "CN")) return 156;
	if(!str_comp(a, "TW")) return 158;
	if(!str_comp(a, "HK")) return 344;
	if(!str_comp(a, "MO")) return 446;
	if(!str_comp(a, "GB")) return 826;
	if(!str_comp(a, "UK")) return 826;
	if(!str_comp(a, "US")) return 840;
	return -1;
}

const char *CLocalization::GetLanguageCodeFromISO(const char *pCountryIso)
{
	return GetLanguageCode(IsoAlpha2ToCountryNum(pCountryIso));
}

const char *CLocalization::Localize(const char *pLanguage, const char *pText)
{
	if(!pLanguage || !pText)
		return pText ? pText : "";
	std::map<std::string, SLanguageFile>::iterator Lang = m_aLocalize.find(pLanguage);
	if(Lang == m_aLocalize.end())
		return pText;
	std::map<std::string, std::string>::iterator It = Lang->second.m_aLocalizedTexts.find(pText);
	if(It == Lang->second.m_aLocalizedTexts.end() || It->second.empty())
		return pText;
	return It->second.c_str();
}

ILocalization *CreateLocalization(IStorage *pStorage) { return new CLocalization(pStorage); }