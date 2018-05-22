//
//  Expression.hpp
//  IntervalTree
//
//  Created by Aaron Graubert on 8/2/17.
//  Copyright © 2017 Aaron Graubert. All rights reserved.
//

#ifndef Expression_h
#define Expression_h

#include "Metrics.h"
#include <vector>
#include <set>
#include <iostream>
#include <api/BamReader.h>
#include <api/BamAlignment.h>

//Utility functions
unsigned int extractBlocks(BamTools::BamAlignment&, std::vector<Feature>&, unsigned short, bool);
//unsigned int legacyExtractBlocks(BamTools::BamAlignment&, std::vector<Feature>&, unsigned short);
std::list<Feature>* intersectBlock(Feature&, std::list<Feature>&);
void trimFeatures(BamTools::BamAlignment&, std::list<Feature>&);
void trimFeatures(BamTools::BamAlignment&, std::list<Feature>&, BaseCoverage&);
void dropFeatures(std::list<Feature>&, BaseCoverage&);

//Metrics functions
unsigned int fragmentSizeMetrics(unsigned int, std::map<unsigned short, std::list<Feature>>*, std::map<std::string, std::string>&, std::list<long long>&, BamTools::SamSequenceDictionary&, std::vector<Feature>&, BamTools::BamAlignment&);

void exonAlignmentMetrics(unsigned int, std::map<unsigned short, std::list<Feature>>&, Metrics&, BamTools::SamSequenceDictionary&, std::map<std::string, double>&, std::map<std::string, double>&, std::vector<Feature>&, BamTools::BamAlignment&, unsigned int, unsigned short, BiasCounter&, BaseCoverage&);

void legacyExonAlignmentMetrics(unsigned int, std::map<unsigned short, std::list<Feature>>&, Metrics&, BamTools::SamSequenceDictionary&, std::map<std::string, double>&, std::map<std::string, double>&, std::vector<Feature>&, BamTools::BamAlignment&, unsigned int, unsigned short, BiasCounter&, BaseCoverage&);

std::string buildSequence(BamTools::BamAlignment&);

#endif /* Expression_h */
