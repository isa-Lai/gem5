#ifndef __STATISTICS_H
#define __STATISTICS_H

#include <string>

// FIXME Find better way to decide where does it come from
#if !defined(RAMULATOR)
#define INTEGRATED_WITH_GEM5
#endif

#ifdef INTEGRATED_WITH_GEM5
using namespace std;
#include "base/statistics.hh"
using namespace  gem5::statistics;
#else
#include "StatType.h"
using namespace Stats;
#endif

/*
  IMPORTANT NOTE - Read this first!

  This version of the file provides wrappers to the gem5 statistics classes.
  Feel free to go through this file, though it can be difficult to follow
  with the degree of abstraction going on. In short, this file currently
  provides the following mapping of stat classes. In almost all cases, the
  wrapper provides identical and complete functionality to the gem5 stat
  classes. All of our classes are defined in the ramulator namespace.

  GEM5 CLASS --> RAMULATOR CLASS
  ==============================
  gem5::Stats::Scalar --> ScalarStat
  gem5::Stats::Average --> AverageStat
  gem5::Stats::Vector --> VectorStat
  gem5::Stats::AverageVector --> AverageVectorStat
  gem5::Stats::Distribution --> DistributionStat
  gem5::Stats::Histogram --> HistogramStat
  gem5::Stats::StandardDeviation --> StandardDeviationStat
  gem5::Stats::AverageDeviation --> AverageDeviationStat

  All of the gem5::Stats that you create will be named "ramulator.<your name>"
  automatically, and will be dumped at the end of simulation into the gem5
  gem5::Stats file.
*/

namespace ramulator {

template<class StatType>
class StatBase { // wrapper for gem5::Stats::DataWrap
  protected:
    StatType stat;
    std::string statName;

    StatBase<StatType> & self() { return *this; }
  public:
    StatBase() {}

#ifndef INTEGRATED_WITH_GEM5
    const StatType* get_stat() const {
      return &stat;
    }
#endif

    StatBase(std::string _name) {
      name(_name);
    }

    StatBase(std::string _name, std::string _desc) {
      name(_name);
      desc(_desc);
    }

    StatBase<StatType> & name(std::string _name) {
      statName = _name;
      stat.name("ramulator." + _name);

      return self();
    }

    const std::string &name(void) const { return statName; }

    StatBase<StatType> & setSeparator(const std::string & _sep) {
      stat.setSeparator(_sep);
      return self();
    }

    const std::string &setSeparator() const { return stat.setSeparator(); }

    StatBase<StatType> & desc(std::string _desc) {
      stat.desc(_desc);
      return self();
    }

    StatBase<StatType> & precision(int _precision) {
      stat.precision(_precision);
      return self();
    }

    StatBase<StatType> & flags( Flags _flags) {
      stat.flags(_flags);
      return self();
    }

    template <class Stat>
    StatBase<StatType> & prereq(const Stat & _prereq) {
      stat.prereq(_prereq);
      return self();
    }

     size_type size(void) const { return stat.size(); }
    bool zero(void) const { return stat.zero(); }
    void prepare(void) { stat.prepare(); }
    void reset(void) { stat.reset(); }
};

template<class StatType>
class StatBaseVec : public StatBase<StatType> { // wrapper for gem5::Stats::DataWrapVec
  protected:
    StatBaseVec<StatType> & self() { return *this; }

  public:
    StatBaseVec<StatType> & subname( off_type index, const std::string & name) {
      StatBase<StatType>::stat.subname(index, name);
      return self();
    }

    StatBaseVec<StatType> & subdesc( off_type index, const std::string & desc) {
      StatBase<StatType>::stat.subdesc(index, desc);
      return self();
    }
};

template<class StatType>
class ScalarStatBase : public StatBase<StatType> { // wrapper for gem5::Stats::ScalarBase
  public:
     Counter value() const { return StatBase<StatType>::stat.value(); };
    void operator++() { ++StatBase<StatType>::stat; }
    void operator--() { --StatBase<StatType>::stat; }

    void operator++(int) { StatBase<StatType>::stat++; }
    void operator--(int) { StatBase<StatType>::stat--; }

    template <typename U>
    void operator=(const U &v) { StatBase<StatType>::stat = v; }

    template <typename U>
    void operator+=(const U &v) { StatBase<StatType>::stat += v; }

    template <typename U>
    void operator-=(const U &v) { StatBase<StatType>::stat -= v; }
};

template<class StatType, class Element>
class VectorStatBase : public StatBaseVec<StatType> { // wrapper for gem5::Stats::VectorBase
  protected:
    VectorStatBase<StatType, Element> & self() { return *this; }

  public:
    void value( VCounter & vec) const { StatBase<StatType>::stat.value(vec); }
    void result( VResult & vec) const { StatBase<StatType>::stat.result(vec); }
     Result total(void) const { return StatBase<StatType>::stat.total(); }

    bool check(void) const { return StatBase<StatType>::stat.check(); }

    VectorStatBase<StatType, Element> & init( size_type size) {
      StatBase<StatType>::stat.init(size);
      return self();
    }

#ifdef INTEGRATED_WITH_GEM5
     ScalarProxy<StatType> operator[]( off_type index) { return StatBase<StatType>::stat[index]; }
#else
    Element &operator[]( off_type index) { return StatBase<StatType>::stat[index]; }
#endif
};


template<class StatType>
class DistStatBase : public StatBase<StatType> { // wrapper for gem5::Stats::DistBase
  public:
    template<typename U>
    void sample(const U &v, int n = 1) { StatBase<StatType>::stat.sample(v, n); }

    void add(DistStatBase & d) { StatBase<StatType>::stat.add(d.StatBase<StatType>::stat); }
};


/*
  nice wrappers for the gem5 gem5::Stats classes used throughout the rest of the code
*/

class ScalarStat : public ScalarStatBase< Scalar> {
  public:
    using ScalarStatBase< Scalar>::operator=;
};

class AverageStat : public ScalarStatBase< Average> {
  public:
    using ScalarStatBase< Average>::operator=;
};

class VectorStat : public VectorStatBase< Vector,  Scalar> {
};

class AverageVectorStat : public VectorStatBase< AverageVector,  Average> {
};

class DistributionStat : public DistStatBase< Distribution> {
  protected:
    DistributionStat & self() { return *this; }

  public:
    DistributionStat & init( Counter min,  Counter max,  Counter bkt) {
      StatBase< Distribution>::stat.init(min, max, bkt);
      return self();
    }

};

class HistogramStat : public DistStatBase< Histogram> {
  protected:
    HistogramStat & self() { return *this; }

  public:
    HistogramStat & init( size_type size) {
      StatBase< Histogram>::stat.init(size);
      return self();
    }
};

class StandardDeviationStat : public DistStatBase< StandardDeviation> {
};

class AverageDeviationStat : public DistStatBase< AverageDeviation> {
};

/*
  RamulatorStats TODO
  * Formula
  * VectorDistribution
  * VectorStandardDeviation
  * VectorAverageDeviation
  * Vector2d
  * SparseHistogram
*/

} /* namespace ramulator */

#endif
