#ifndef __MetricAccessorInband_hpp__
#define __MetricAccessorInband_hpp__

#include "MetricAccessor.hpp"


class MetricAccessorInband : public MetricAccessor {
public:
  MetricAccessorInband(Prof::Metric::IData &_mdata) : mdata(_mdata) {}
  ~MetricAccessorInband() {}
  double &idx(unsigned int mId, unsigned int size = 0) {
    return mdata.idx(mId, size);
  }
  double c_idx(unsigned int mId) const {
    return mdata.c_idx(mId);
  }
private:
  Prof::Metric::IData &mdata;
};

#endif
