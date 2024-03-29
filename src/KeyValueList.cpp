#include "KeyValueList.h"
#include "otadrive_esp.h"

using namespace OTAdrive_ns;

String KeyValueList::value(String key)
{
    String result = "";
    key += "=";
    const char *v = vals.c_str();
    const int vlen = vals.length();
    int i = 0, line = 0;
    for (; i < vlen; i++)
    {
        // search for key at start of the line
        for (int j = 0; i < vlen;)
        {
            if (v[i] != key[j])
                break;
            j++;
            i++;
            if (j == key.length())
            {
                for (; i < vlen; i++)
                {
                    if (v[i] == '\n')
                        break;
                    result += v[i];
                }
                otd_log_d("result %s=%s", key.c_str(), result.c_str());
                return result;
            }
        }

        // skip to end of line
        for (; i < vlen; i++)
        {
            if (v[i] == '\n')
            {
                line++;
                break;
            }
        }
    }

    otd_log_e("config key %s not found", key.c_str());
    return "";
}

bool KeyValueList::containsKey(String key)
{
    key += "=";
    const char *v = vals.c_str();
    const int vlen = vals.length();
    int i = 0, line = 0;
    for (; i < vlen; i++)
    {
        // search for key at start of the line
        for (int j = 0; i < vlen;)
        {
            if (v[i] != key[j])
                break;
            j++;
            i++;
            if (j == key.length())
            {
                return true;
            }
        }

        // skip to end of line
        for (; i < vlen; i++)
        {
            if (v[i] == '\n')
            {
                line++;
                break;
            }
        }
    }

    otd_log_e("config key %s not found", key.c_str());
    return false;
}

void KeyValueList::load(String vals)
{
    this->vals = vals;
}

bool KeyValueList::hasAny()
{
    return this->vals.length() > 0;
}