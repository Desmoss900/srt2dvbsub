/*
* Copyright (c) 2025 Mark E. Rosche, Chili IPTV Systems
* All rights reserved.
*
* PERSONAL USE LICENSE - NON-COMMERCIAL ONLY
* ────────────────────────────────────────────────────────────────
* This software is provided for personal, educational, and non-commercial
* use only. You are granted permission to use, copy, and modify this
* software for your own personal or educational purposes, provided that
* this copyright and license notice appears in all copies or substantial
* portions of the software.
*
* PERMITTED USES:
*   ✓ Personal projects and experimentation
*   ✓ Educational purposes and learning
*   ✓ Non-commercial testing and evaluation
*   ✓ Individual hobbyist use
*
* PROHIBITED USES:
*   ✗ Commercial use of any kind
*   ✗ Incorporation into products or services sold for profit
*   ✗ Use within organizations or enterprises for revenue-generating activities
*   ✗ Modification, redistribution, or hosting as part of any commercial offering
*   ✗ Licensing, selling, or renting this software to others
*   ✗ Using this software as a foundation for commercial services
*
* No commercial license is available. For inquiries regarding any use not
* explicitly permitted above, contact:
*   Mark E. Rosche, Chili IPTV Systems
*   Email: license@chili-iptv.de
*   Website: www.chili-iptv.de
*
* ────────────────────────────────────────────────────────────────
* DISCLAIMER
* ────────────────────────────────────────────────────────────────
* THIS SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
* OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
* DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
* ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*
* ────────────────────────────────────────────────────────────────
* By using this software, you agree to these terms and conditions.
* ────────────────────────────────────────────────────────────────
*/

#include "dvb_lang.h"

/* Immutable DVB language lookup table (definition). */
const struct dvb_lang_entry dvb_langs[] = {
    {"eng", "English", "English"},
    {"deu", "German", "Deutsch"},
    {"fra", "French", "Français"},
    {"spa", "Spanish", "Español"},
    {"ita", "Italian", "Italiano"},
    {"por", "Portuguese", "Português"},
    {"rus", "Russian", "Русский"},
    {"jpn", "Japanese", "日本語"},
    {"zho", "Chinese", "中文"},
    {"kor", "Korean", "한국어"},
    {"nld", "Dutch", "Nederlands"},
    {"swe", "Swedish", "Svenska"},
    {"dan", "Danish", "Dansk"},
    {"nor", "Norwegian", "Norsk"},
    {"fin", "Finnish", "Suomi"},
    {"pol", "Polish", "Polski"},
    {"ces", "Czech", "Čeština"},
    {"slk", "Slovak", "Slovenčina"},
    {"slv", "Slovenian", "Slovenščina"},
    {"hrv", "Croatian", "Hrvatski"},
    {"ron", "Romanian", "Română"},
    {"bul", "Bulgarian", "Български"},
    {"ukr", "Ukrainian", "Українська"},
    {"bel", "Belarusian", "Беларуская"},
    {"est", "Estonian", "Eesti"},
    {"lav", "Latvian", "Latviešu"},
    {"lit", "Lithuanian", "Lietuvių"},
    {"hun", "Hungarian", "Magyar"},
    {"heb", "Hebrew", "עברית"},
    {"ara", "Arabic", "العربية"},
    {"tur", "Turkish", "Türkçe"},
    {"ell", "Greek", "Ελληνικά"},
    {"cat", "Catalan", "Català"},
    {"gle", "Irish", "Gaeilge"},
    {"eus", "Basque", "Euskara"},
    {"glg", "Galician", "Galego"},
    {"srp", "Serbian", "Српски"},
    {"mkd", "Macedonian", "Македонски"},
    {"alb", "Albanian", "Shqip"},
    {"hin", "Hindi", "हिन्दी"},
    {"tam", "Tamil", "தமிழ்"},
    {"tel", "Telugu", "తెలుగు"},
    {"pan", "Punjabi", "ਪੰਜਾਬੀ"},
    {"urd", "Urdu", "اردو"},
    {"vie", "Vietnamese", "Tiếng Việt"},
    {"tha", "Thai", "ไทย"},
    {"ind", "Indonesian", "Bahasa Indonesia"},
    {"msa", "Malay", "Bahasa Melayu"},
    {"sin", "Sinhala", "සිංහල"},
    {"khm", "Khmer", "ភាសាខ្មែរ"},
    {"lao", "Lao", "ລາວ"},
    {"mon", "Mongolian", "Монгол"},
    {"fas", "Persian", "فارسی"},
    {NULL, NULL, NULL}
};

int is_valid_dvb_lang(const char *code)
{
    if (!code)
        return 0;

    size_t len = strlen(code);
    if (len != 3)
        return 0;

    char low[4];
    for (int i = 0; i < 3; i++)
    {
        if (!isalpha((unsigned char)code[i]))
            return 0;
        low[i] = tolower((unsigned char)code[i]);
    }
    low[3] = '\0';

    for (const struct dvb_lang_entry *p = dvb_langs; p->code; ++p)
    {
        if (strcmp(low, p->code) == 0)
            return 1;
    }
    return 0;
}
