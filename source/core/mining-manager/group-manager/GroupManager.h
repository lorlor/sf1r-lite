///
/// @file GroupManager.h
/// @brief manage forward index for string property values (doc id => value id)
/// @author Jun Jiang <jun.jiang@izenesoft.com>
/// @date Created 2011-03-22
///

#ifndef SF1R_GROUP_MANAGER_H_
#define SF1R_GROUP_MANAGER_H_

#include "PropValueTable.h"
#include "../faceted-submanager/ontology_rep.h"
#include <configuration-manager/GroupConfig.h>

#include <vector>
#include <string>
#include <map>

namespace sf1r
{
class DocumentManager;
}

NS_FACETED_BEGIN

class GroupManager {
public:
    typedef std::map<std::string, PropValueTable> PropValueMap;

    GroupManager(
        DocumentManager* documentManager,
        const std::string& dirPath
    );

    /**
     * @brief Open the properties which need group result.
     * @param configVec the property configs
     * @return true for success, false for failure
     */
    bool open(const std::vector<GroupConfig>& configVec);

    /**
     * @brief Build group index data for the whole collection.
     * @return true for success, false for failure
     */
    bool processCollection();

    const PropValueTable* getPropValueTable(const std::string& propName) const
    {
        PropValueMap::const_iterator it = propValueMap_.find(propName);
        if (it != propValueMap_.end())
        {
            return &(it->second);
        }
        return NULL;
    }

    PropValueTable* getPropValueTable(const std::string& propName)
    {
        PropValueMap::iterator it = propValueMap_.find(propName);
        if (it != propValueMap_.end())
        {
            return &(it->second);
        }
        return NULL;
    }

private:
    void buildDoc_(
        docid_t docId,
        const std::string& propName,
        PropValueTable& pvTable
    );

private:
    sf1r::DocumentManager* documentManager_;
    std::string dirPath_;

    PropValueMap propValueMap_;
};

NS_FACETED_END

#endif 