//
//  BED.cpp
//  IntervalTree
//
//  Created by Aaron Graubert on 7/11/17.
//  Copyright © 2017 Aaron Graubert. All rights reserved.
//

#include "BED.h"
#include <sstream>
#include <exception>
#include <stdexcept>

using std::ifstream;
using std::string;

ifstream& extractBED(ifstream &input, Feature &out)
{
    try
    {
        string line;
        while(getline(input, line))
        {
            if(line[0] == '#') continue; //Do beds even have comment lines?
            std::istringstream tokenizer(line);
            string buffer;
            tokenizer >> buffer; //chromosome name
            //add a standardizing function for chr# -> # and [XYM/etc] -> <unmodified>
            out.chromosome = chromosomeMap(buffer.substr(3));
            tokenizer >> buffer; //start
            out.start = std::stoull(buffer) + 1;
            tokenizer >> buffer; //stop
            out.end = std::stoull(buffer) + 1;
            out.exon_id = line; // add a dummy exon_id for mapping interval intersections later
            out.type = "exon";
            break;
        }
    }
    catch (std::exception &e)
    {
        std::cout << "Here's what happened: "<<e.what()<<std::endl;
    }
    return input;
}
