#ifndef SF1R_COMMON_NUMERIC_RANGE_PROPERTY_TABLE_H
#define SF1R_COMMON_NUMERIC_RANGE_PROPERTY_TABLE_H

#include "NumericPropertyTableBase.h"
#include <util/modp_numtoa.h>

#include <boost/lexical_cast.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <vector>
#include <iostream>
#include <sstream>
#include <iomanip>

namespace sf1r
{

namespace detail
{

static const std::string delimiters("-~,");

template <class T>
bool convert(const std::string& pattern, T& result)
{
    try
    {
        result = boost::lexical_cast<T>(pattern);
    }
    catch (const boost::bad_lexical_cast &)
    {
        return false;
    }
    return true;
}

template <>
bool convert<int8_t>(const std::string& pattern, int8_t& result)
{
    try
    {
        result = boost::numeric_cast<int8_t>(boost::lexical_cast<int32_t>(pattern));
    }
    catch (const boost::bad_lexical_cast &)
    {
        return false;
    }
    return true;
}

template <class T>
bool split_numeric(const std::string& pattern, std::pair<T, T>& result)
{
    std::size_t pos = pattern.find_first_of(delimiters);
    if (pos == std::string::npos)
    {
        if (convert(pattern, result.first))
        {
            result.second = result.first;
            return true;
        }
        return false;
    }

    std::pair<T, T> tmpResult;
    if (!convert(pattern.substr(0, pos), tmpResult.first))
        return false;
    if (!convert(pattern.substr(pos + 1), tmpResult.second))
        return false;

    if (tmpResult.first > tmpResult.second)
    {
        result.first = tmpResult.second;
        result.second = tmpResult.first;
    }
    else
    {
        result = tmpResult;
    }
    return true;
}

}

template <class T>
class NumericRangePropertyTable : public NumericPropertyTableBase
{
    typedef std::pair<T, T> value_type;

public:
    NumericRangePropertyTable(PropertyDataType type)
        : NumericPropertyTableBase(type)
        , dirty_(false)
        , invalidValue_(std::make_pair(std::numeric_limits<T>::max(),
                                       std::numeric_limits<T>::max()))
        , data_(1, invalidValue_)
    {
    }

    NumericRangePropertyTable(PropertyDataType type, const value_type& initValue)
        : NumericPropertyTableBase(type)
        , dirty_(false)
        , invalidValue_(initValue)
        , data_(1, invalidValue_)
    {
    }

    ~NumericRangePropertyTable()
    {
    }

    void init(const std::string& path)
    {
        path_ = path;
        std::ifstream ifs(path_.c_str());
        if (ifs) load_(ifs);
    }

    void resize(std::size_t size)
    {
        ScopedWriteLock lock(mutex_);
        data_.resize(size, invalidValue_);
    }

    std::size_t size(bool isLock) const
    {
        ScopedReadBoolLock lock(mutex_, isLock);
        return data_.size();
    }

    void flush()
    {
        if (!dirty_) return;
        dirty_ = false;
        std::vector<value_type>(data_).swap(data_);
        if (path_.empty()) return;
        std::ofstream ofs(path_.c_str());
        if (ofs) save_(ofs);
    }

    bool isValid(std::size_t pos, bool isLock) const
    {
        ScopedReadBoolLock lock(mutex_, isLock);
        if (pos >= data_.size() || data_[pos] == invalidValue_)
            return false;
        return true;
    }

    bool getInt32Value(std::size_t pos, int32_t& value, bool isLock) const
    {
        ScopedReadBoolLock lock(mutex_, isLock);
        if (pos >= data_.size() || data_[pos] == invalidValue_)
            return false;

        value = static_cast<int32_t>(data_[pos].first);
        return true;
    }
    bool getFloatValue(std::size_t pos, float& value, bool isLock) const
    {
        ScopedReadBoolLock lock(mutex_, isLock);
        if (pos >= data_.size() || data_[pos] == invalidValue_)
            return false;

        value = static_cast<float>(data_[pos].first);
        return true;
    }
    bool getInt64Value(std::size_t pos, int64_t& value, bool isLock) const
    {
        ScopedReadBoolLock lock(mutex_, isLock);
        if (pos >= data_.size() || data_[pos] == invalidValue_)
            return false;

        value = static_cast<int64_t>(data_[pos].first);
        return true;
    }
    bool getDoubleValue(std::size_t pos, double& value, bool isLock) const
    {
        ScopedReadBoolLock lock(mutex_, isLock);
        if (pos >= data_.size() || data_[pos] == invalidValue_)
            return false;

        value = static_cast<double>(data_[pos].first);
        return true;
    }
    bool getStringValue(std::size_t pos, std::string& value, bool isLock) const
    {
        ScopedReadBoolLock lock(mutex_, isLock);
        if (pos >= data_.size() || data_[pos] == invalidValue_)
            return false;

        const value_type& data = data_[pos];
        if (data.first == data.second)
        {
            value = boost::lexical_cast<std::string>(data.first);
        }
        else
        {
            value = boost::lexical_cast<std::string>(data.first);
            value += "-";
            value += boost::lexical_cast<std::string>(data.second);
        }
        return true;
    }
    bool getDoublePairValue(std::size_t pos, std::pair<double, double>& value, bool isLock) const
    {
        ScopedReadBoolLock lock(mutex_, isLock);
        if (pos >= data_.size() || data_[pos] == invalidValue_)
            return false;

        value.first = static_cast<double>(data_[pos].first);
        value.second = static_cast<double>(data_[pos].second);
        return true;
    }
    bool getInt64PairValue(std::size_t pos, std::pair<int64_t, int64_t>& value, bool isLock) const
    {
        ScopedReadBoolLock lock(mutex_, isLock);
        if (pos >= data_.size() || data_[pos] == invalidValue_)
            return false;

        value.first = static_cast<int64_t>(data_[pos].first);
        value.second = static_cast<int64_t>(data_[pos].second);
        return true;
    }

    bool getValue(std::size_t pos, value_type& value, bool isLock = true) const
    {
        ScopedReadBoolLock lock(mutex_, isLock);
        if (pos >= data_.size() || data_[pos] == invalidValue_)
            return false;

        value = data_[pos];
        return true;
    }

    void* getValueList()
    {
        if (!data_.empty())
            return static_cast<void*>(&data_[0]);
        else
            return NULL;
    }
    const void* getValueList() const
    {
        if (!data_.empty())
            return static_cast<const void*>(&data_[0]);
        else
            return NULL;
    }

    void setInt32Value(std::size_t pos, const int32_t& value)
    {
        if (pos >= data_.size())
        {
            ScopedWriteBoolLock lock(mutex_, true);
            if (pos >= data_.size())
                data_.resize(pos + 1, invalidValue_);
        }

        ScopedReadBoolLock lock(mutex_, true);
        data_[pos].first = data_[pos].second = static_cast<T>(value);
        dirty_ = true;
    }
    void setFloatValue(std::size_t pos, const float& value)
    {
        if (pos >= data_.size())
        {
            ScopedWriteBoolLock lock(mutex_, true);
            if (pos >= data_.size())
                data_.resize(pos + 1, invalidValue_);
        }

        ScopedReadBoolLock lock(mutex_, true);
        data_[pos].first = data_[pos].second = static_cast<T>(value);
        dirty_ = true;
    }
    void setInt64Value(std::size_t pos, const int64_t& value)
    {
        if (pos >= data_.size())
        {
            ScopedWriteBoolLock lock(mutex_, true);
            if (pos >= data_.size())
                data_.resize(pos + 1, invalidValue_);
        }

        ScopedReadBoolLock lock(mutex_, true);
        data_[pos].first = data_[pos].second = static_cast<T>(value);
        dirty_ = true;
    }
    void setDoubleValue(std::size_t pos, const double& value)
    {
        if (pos >= data_.size())
        {
            ScopedWriteBoolLock lock(mutex_, true);
            if (pos >= data_.size())
                data_.resize(pos + 1, invalidValue_);
        }

        ScopedReadBoolLock lock(mutex_, true);
        data_[pos].first = data_[pos].second = static_cast<T>(value);
        dirty_ = true;
    }
    bool setStringValue(std::size_t pos, const std::string& value)
    {
        if (pos >= data_.size())
        {
            ScopedWriteBoolLock lock(mutex_, true);
            if (pos >= data_.size())
                data_.resize(pos + 1, invalidValue_);
        }

        ScopedReadBoolLock lock(mutex_, true);
        if (detail::split_numeric(value, data_[pos]))
        {
            dirty_ = true;
            return true;
        }
        return false;
    }

    void setValue(std::size_t pos, const value_type& value)
    {
        if (pos >= data_.size())
        {
            ScopedWriteBoolLock lock(mutex_, true);
            if (pos >= data_.size())
                data_.resize(pos + 1, invalidValue_);
        }

        ScopedReadBoolLock lock(mutex_, true);
        data_[pos] = value;
        dirty_ = true;
    }

    void copyValue(std::size_t from, std::size_t to)
    {
        ScopedWriteLock lock(mutex_);
        if (from >= data_.size() || data_[from] == invalidValue_)
            return;

        if (to >= data_.size())
            data_.resize(to + 1, invalidValue_);

        data_[to] = data_[from];
        dirty_ = true;
    }

    int compareValues(std::size_t lhs, std::size_t rhs, bool isLock) const
    {
        ScopedReadBoolLock lock(mutex_, isLock);
        const value_type& lv = data_[lhs];
        if (lv == invalidValue_) return -1;
        const value_type& rv = data_[rhs];
        if (rv == invalidValue_) return 1;
        if (lv < rv) return -1;
        if (lv > rv) return 1;
        return 0;
    }

    void clearValue(std::size_t pos)
    {
        ScopedWriteLock lock(mutex_);
        if (pos < data_.size())
        {
            data_[pos] = invalidValue_;
            dirty_ = true;
        }
    }

protected:
    void load_(std::istream& is)
    {
        ScopedWriteLock lock(mutex_);
        std::size_t len = 0;
        is.read((char*)&len, sizeof(len));
        data_.resize(len);
        is.read((char*)&data_[0], sizeof(value_type) * len);
    }

    void save_(std::ostream& os) const
    {
        ScopedReadLock lock(mutex_);
        std::size_t len = data_.size();
        os.write((const char*)&len, sizeof(len));
        os.write((const char*)&data_[0], sizeof(value_type) * len);
    }

protected:
    bool dirty_;
    value_type invalidValue_;
    std::string path_;
    std::vector<value_type> data_;
};

template <>
inline bool NumericRangePropertyTable<int64_t>::getStringValue(std::size_t pos, std::string& value, bool isLock) const
{
    ScopedReadBoolLock lock(mutex_, isLock);
    if (pos >= data_.size() || data_[pos] == invalidValue_)
        return false;

    using namespace boost::posix_time;
    const value_type& data = data_[pos];
    if (data.first == data.second)
    {
        if (type_ == DATETIME_PROPERTY_TYPE)
        {
            value = to_iso_string(from_time_t(data.first - timezone));
        }
        else
        {
            value = boost::lexical_cast<std::string>(data.first);
        }
    }
    else
    {
        if (type_ == DATETIME_PROPERTY_TYPE)
        {
            value = to_iso_string(from_time_t(data.first - timezone));
            value += "-";
            value += to_iso_string(from_time_t(data.second - timezone));
        }
        else
        {
            value = boost::lexical_cast<std::string>(data.first);
            value += "-";
            value += boost::lexical_cast<std::string>(data.second);
        }
    }
    return true;
}

template <>
inline bool NumericRangePropertyTable<float>::getStringValue(std::size_t pos, std::string& value, bool isLock) const
{
    ScopedReadBoolLock lock(mutex_, isLock);
    if (pos >= data_.size() || data_[pos] == invalidValue_)
        return false;

    const value_type& data = data_[pos];
    if (data.first == data.second)
    {
        char buf[32];
        modp_dtoa((double)data.first, buf, 2);
        value.assign(buf);
    }
    else
    {
        char buf1[32],buf2[32],buf3[64];
        modp_dtoa((double)data.first, buf1, 2);
        modp_dtoa((double)data.second, buf2, 2);
        sprintf(buf3,"%s-%s",buf1,buf2);
        value.assign(buf3);
    }
    return true;
}

template <>
inline bool NumericRangePropertyTable<int8_t>::getStringValue(std::size_t pos, std::string& value, bool isLock) const
{
    ScopedReadBoolLock lock(mutex_, isLock);
    if (pos >= data_.size() || data_[pos] == invalidValue_)
        return false;

    static std::stringstream ss;
    const value_type& data = data_[pos];
    if (data.first == data.second)
    {
        ss << boost::numeric_cast<int32_t>(data.first);
    }
    else
    {
        ss << boost::numeric_cast<int32_t>(data.first) << "-" << boost::numeric_cast<int32_t>(data.second);
    }
    value = ss.str();
    ss.str(std::string());
    return true;
}

}

#endif
