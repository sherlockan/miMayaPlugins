#ifndef __UVEDGE__
#define __UVEDGE__

#include "uvPoint.h"
#include <utility>


class UvEdge {
public:
    UvEdge();
    UvEdge(UvPoint beginPt, UvPoint endPt, std::pair<int, int> index);
    ~UvEdge();

    UvPoint begin;
    UvPoint end;
    std::pair<int, int> index;

    bool operator==(const UvEdge& rhs) const;
    inline bool operator!=(const UvEdge& rhs) const
    {
        return !(*this == rhs);
    }

    bool operator>(const UvEdge& rhs) const;
    bool operator>=(const UvEdge& rhs) const;
    bool operator<(const UvEdge& rhs) const;
    bool operator<=(const UvEdge& rhs) const;

    bool isIntersected(UvEdge& otherEdge);
    float getTriangleArea(float x1,
                          float y1,
                          float x2,
                          float y2,
                          float x3,
                          float y3);

private:
};

#endif /* defined(__UVEDGE_H__) */
