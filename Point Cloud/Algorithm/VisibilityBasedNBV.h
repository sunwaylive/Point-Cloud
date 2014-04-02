#ifndef VISIBILITY_BASED_NBV_H
#define VISIBILITY_BASED_NBV_H

#pragma once
#include <iostream>
#include "PointCloudAlgorithm.h"
#include "GlobalFunction.h"

class VisibilityBasedNBV : PointCloudAlgorithm
{
public:
  VisibilityBasedNBV(RichParameterSet* _para);
  ~VisibilityBasedNBV();

  void run();
  void setInput(DataMgr *pData);
  void setParameterSet(RichParameterSet* _para) {para = _para}
  RichParameterSet* getParameterSet() {return para;}
  void clear();

private:
  RichParameterSet *para;
  double           optimalDist;
  CMesh            *original;
};
#endif