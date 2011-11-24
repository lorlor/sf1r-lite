#ifndef _LOG_MANAGER_UTIL_FUNCTIONS_H_
#define _LOG_MANAGER_UTIL_FUNCTIONS_H_

#include <util/ustring/UString.h>

#include <string>
#include <boost/date_time/posix_time/posix_time.hpp>

namespace sf1r
{

template <typename T>
std::string toBytes(const T& val)
{
    return std::string(reinterpret_cast<const char *>(&val), sizeof(T));
}

template <typename T>
T fromBytes(const std::string& str)
{
    return *(reinterpret_cast<const T *>(str.c_str()));
}

template <typename Key, typename T>
void getKeyList(std::vector<Key>& key_list, const std::map<Key, T>& src_map)
{
    for (typename std::map<Key, T>::const_iterator it = src_map.begin();
            it != src_map.end(); ++it)
    {
        key_list.push_back(it->first);
    }
}

template <typename Key, typename T>
void getValueList(std::vector<T>& value_list, const std::map<Key, T>& src_map)
{
    for (typename std::map<Key, T>::const_iterator it = src_map.begin();
            it != src_map.end(); ++it)
    {
        value_list.push_back(it->second);
    }
}

time_t createTimeStamp();

time_t createTimeStamp(boost::posix_time::ptime pt);

time_t createTimeStamp(const izenelib::util::UString& text);

time_t createTimeStamp(const std::string& text);

struct KeyLess
{
    template <typename T1, typename T2>
    bool operator()(const std::pair<T1, T2>& p1, const std::pair<T1, T2>& p2) const
    {
        return p1.first < p2.first;
    }
};

}

#endif
