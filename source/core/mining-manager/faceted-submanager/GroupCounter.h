///
/// @file GroupCounter.h
/// @brief count docs for property values
/// @author Jun Jiang <jun.jiang@izenesoft.com>
/// @date Created 2011-07-29
///

#ifndef SF1R_GROUP_COUNTER_H
#define SF1R_GROUP_COUNTER_H

#include "faceted_types.h"
#include "ontology_rep.h"
#include "prop_value_table.h"

#include <vector>

NS_FACETED_BEGIN

class GroupCounter
{
public:
    GroupCounter(const PropValueTable& pvTable);

    void addDoc(docid_t doc);

    void getGroupRep(OntologyRep& groupRep);

private:
    /**
     * append all descendent nodes of @p pvId to @p itemList.
     * @param itemList the nodes are appended to this list
     * @param pvId the value id of the root node to append
     * @param level the level of the root node to append
     * @param valueStr the string value of the root node
     */
    void appendGroupRep(
        std::list<OntologyRepItem>& itemList,
        PropValueTable::pvid_t pvId,
        int level,
        const izenelib::util::UString& valueStr
    );

private:
    const PropValueTable& propValueTable_;

    const std::vector<PropValueTable::PropStrMap>& childMapTable_;

    /** map from value id to doc count */
    std::vector<int> countTable_;
};

NS_FACETED_END

#endif 