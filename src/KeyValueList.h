#include <Arduino.h>

namespace OTAdrive_ns
{
    class KeyValueList
    {
    private:
        String vals;

    public:
        bool hasAny();
        void load(String vals);
        String value(String key);
        bool containsKey(String key);
    };
}