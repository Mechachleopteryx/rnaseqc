// IntervalTree.cpp : Defines the entry point for the console application.


//Include headers
#include "BED.h"
#include "Metrics.h"
#include "Expression.h"
#include <string>
#include <iostream>
#include <iterator>
#include <stdio.h>
#include <set>
#include <tuple>
#include <regex>
#include <list>
#include <ctime>
#include <limits.h>
#include <math.h>
#include <api/BamReader.h>
#include <api/BamAlignment.h>
#include <api/BamConstants.h>
#include <args.hxx>
#include <boost/filesystem.hpp>
using namespace std;
using namespace args;
using namespace BamTools;

const string NM = "NM";
const double MAD_FACTOR = 1.4826;
map<string, double> tpms;

bool compGenes(const string&, const string&);
template <typename T> double computeMedian(unsigned long, T&&, unsigned int=0u);
void add_range(vector<unsigned long>&, coord, unsigned int);
tuple<double, double, double> computeCoverage(ofstream&, string&, string&, const double, map<string, vector<CoverageEntry> >&, vector<string>&);

int main(int argc, char* argv[])
{
    //Set up command line syntax
    ArgumentParser parser("RNASeQC 2.0.0");
    HelpFlag help(parser, "help", "Display this message and quit", {'h', "help"});
    Positional<string> gtfFile(parser, "gtf", "The input GTF file containing features to check the bam against");
    Positional<string> bamFile(parser, "bam", "The input SAM/BAM file containing reads to process");
    Positional<string> outputDir(parser, "output", "Output directory");
    ValueFlag<string> sampleName(parser, "sample", "The name of the current sample.  Default: The bam's filename", {'s', "sample"});
    ValueFlag<string> bedFile(parser, "BEDFILE", "Optional input BED file containing non-overlapping exons used for fragment size calculations", {"bed"});
    ValueFlag<int> chimericDistance(parser, "DISTANCE", "Set the maximum accepted distance between read mates.  Mates beyond this distance will be counted as chimeric pairs. Default: 2000000 [bp]", {"chimeric-distance"});
    ValueFlag<unsigned int> maxReadLength(parser, "LENGTH", "Set the maximum accepted length.  Reads longer than this threshold are discarded. Default: 1000000 [bp]", {"read-length"});
    ValueFlag<unsigned int> fragmentSamples(parser, "SAMPLES", "Set the number of samples to take when computing fragment sizes.  Requires the --bed argument. Default: 1000000", {"fragment-samples"});
    ValueFlag<unsigned int> lowQualityThreshold(parser, "QUALITY", "Set the lower bound on read quality. Reads below this number are counted as low quality BUT ARE STILL USED IN COUNTS. See --mapping-quality to discard reads based on quality. Default: 255", {"low-quality"});
    ValueFlag<unsigned int> mappingQualityThreshold(parser,"QUALITY", "Set the lower bound on read quality for exon coverage counting. Reads below this number are excluded from coverage metrics. Default: 255", {"mapping-quality"});
    ValueFlag<unsigned int> baseMismatchThreshold(parser, "MISMATCHES", "Set the maximum number of allowed mismatches between a read and the reference sequence. Reads with more than this number of mismatches are excluded from coverage metrics. Default: 6", {"base-mismatch"});
    ValueFlag<int> splitDistance(parser, "DISTANCE", "Set the minimum distance between aligned blocks of a read for the read to be counted as split. Default: 100 [bp]", {"split-distance"});
    ValueFlag<int> biasOffset(parser, "OFFSET", "Set the offset into the gene for the 3' and 5' windows in bias calculation.  A positive value shifts the 3' and 5' windows towards eachother, while a negative value shifts them apart.  Default: 150 [bp]", {"offset"});
    ValueFlag<int> biasWindow(parser, "SIZE", "Set the size of the 3' and 5' windows in bias calculation.  Default: 100 [bp]", {"window-size"});
    ValueFlag<unsigned long> biasGeneLength(parser, "LENGTH", "Set the minimum size of a gene for bias calculation.  Genes below this size are ignored in the calculation.  Default: 600 [bp]", {"gene-length"});
    Flag LegacyMode(parser, "legacy", "Use legacy counting rules.  Gene and exon counts match output of RNA-SeQC 1.1.9", {"legacy"});
    ValueFlag<string> strandSpecific(parser, "stranded", "Use strand-specific metrics. Only features on the same strand of a read will be considered.  Allowed values are 'RF', 'rf', 'FR', and 'fr'", {"stranded"});
    CounterFlag verbosity(parser, "verbose", "Give some feedback about what's going on.  Supply this argument twice for progress updates while parsing the bam", {'v', "verbose"});
    ValueFlagList<string> filterTags(parser, "TAG", "Filter out reads with the specified tag.", {'t', "tag"});
    ValueFlag<string> chimericTag(parser, "TAG", "Reads maked with the specified tag will be labeled as Chimeric.  Defaults to 'mC' for STAR", {"chimeric-tag"});
    Flag excludeChimeric(parser, "exclude-chimeric", "Exclude chimeric reads from the read counts", {"exclude-chimeric"});
    Flag unpaired(parser, "unparied", "Treat all reads as unpaired, ignoring filters which require properly paired reads", {'u', "unpaired"});
    Flag useRPKM(parser, "rpkm", "Output gene RPKM values instead of TPMs", {"rpkm"});
    Flag outputTranscriptCoverage(parser, "coverage", "If this flag is provided, coverage statistics for each transcript will be written to a table. Otherwise, only summary coverage statistics are generated and added to the metrics table", {"coverage"});
	try
	{
        //parse and validate the command line arguments
        parser.ParseCLI(argc, argv);
        if (!gtfFile) throw ValidationError("No GTF file provided");
        if (!bamFile) throw ValidationError("No BAM file provided");
        if (!outputDir) throw ValidationError("No output directory provided");

        unsigned short STRAND_SPECIFIC = 0;
        if (strandSpecific)
        {
            string tmp_strand = strandSpecific.Get();
            if (tmp_strand == "RF" || tmp_strand == "rf") STRAND_SPECIFIC = 1;
            else if(tmp_strand == "FR" || tmp_strand == "fr") STRAND_SPECIFIC = -1;
            else throw ValidationError("--stranded argument must be in {'RF', 'rf', 'FR', 'fr'}");
        }

        const int CHIMERIC_DISTANCE = chimericDistance ? chimericDistance.Get() : 2000000;
        const unsigned int MAX_READ_LENGTH = maxReadLength ? maxReadLength.Get() : 1000000u;
        const unsigned int FRAGMENT_SIZE_SAMPLES = fragmentSamples ? fragmentSamples.Get() : 1000000u;
        const unsigned int LOW_QUALITY_READS_THRESHOLD = lowQualityThreshold ? lowQualityThreshold.Get() : 255u;
        const unsigned int BASE_MISMATCH_THRESHOLD = baseMismatchThreshold ? baseMismatchThreshold.Get() : 6u;
        const unsigned int MAPPING_QUALITY_THRESHOLD = mappingQualityThreshold ? mappingQualityThreshold.Get() : 255u;
        const int SPLIT_DISTANCE = 100;
        const int VERBOSITY = verbosity ? verbosity.Get() : 0;
        const int BIAS_OFFSET = biasOffset ? biasOffset.Get() : 0; //150
        const int BIAS_WINDOW = biasWindow ? biasWindow.Get() : 100;
        const unsigned long BIAS_LENGTH = biasGeneLength ? biasGeneLength.Get() : 200u; //600
        const vector<string> tags = filterTags ? filterTags.Get() : vector<string>();
        const string chimeric_tag = chimericTag ? chimericTag.Get() : "mC";
        const string SAMPLENAME = sampleName ? sampleName.Get() : boost::filesystem::path(bamFile.Get()).filename().string();

        time_t t0, t1, t2; //various timestamps to record execution time
        clock_t start_clock = clock(); //timer used to compute CPU time
        map<unsigned short, list<Feature>> features; //map of chr -> genes/exons; parsed from GTF
        map<string, Feature> transcripts; //map of chr -> transcripts; for coverage metrics later
        //Parse the GTF and extract features
        {
            Feature line; //current feature being read from the gtf
            ifstream reader(gtfFile.Get());
            if (!reader.is_open())
            {
                cerr << "Unable to open GTF file: " << gtfFile.Get() << endl;
                return 10;
            }

            if (VERBOSITY) cout<<"Reading GTF Features..."<<endl;
            time(&t0);
            while ((reader >> line))
            {
                if(LegacyMode.Get() && line.end == line.start)
                {
                    //legacy code excludes single base exons
                    if (VERBOSITY > 1) cerr<<"Legacy mode excluded feature: " << line.feature_id << endl;
                    if (line.type == "exon") transcriptCodingLengths[line.gene_id] -= 1;
                    continue;
                }
                //Just keep genes and exons.  We don't care about transcripts or any other feature types
                if (line.type == "gene" || line.type == "exon")
                {
                    features[line.chromosome].push_back(line);
                }
                else if (line.type == "transcript") transcripts[line.feature_id] = line;
            }
        }
		//ensure that the features are sorted.  This MUST be true for the exon alignment metrics
        for (auto beg = features.begin(); beg != features.end(); ++beg) beg->second.sort(compIntervalStart);
        time(&t1); //record the time taken to parse the GTF
        if (VERBOSITY) cout << "Finished processing GTF in " << difftime(t1, t0) << " seconds" << endl;

        //fragment size variables
        unsigned int doFragmentSize = 0u; //count of remaining fragment size samples to record
        map<unsigned short, list<Feature> > *bedFeatures; //similar map, but parsed from BED for fragment sizes only
        map<string, string> fragments; //Map of alignment name -> exonID to ensure mates map to the same exon for
        list<long long> fragmentSizes; //list of fragment size samples taken so far
        if (bedFile) //If we were given a BED file, parse it for fragment size calculations
        {
             Feature line; //current feature being read from the bed
            if (VERBOSITY) cout << "Parsing BED intervals for fragment size computations..." << endl;
            doFragmentSize = FRAGMENT_SIZE_SAMPLES;
            bedFeatures = new map<unsigned short, list<Feature> >();
            ifstream bedReader(bedFile.Get());
            if (!bedReader.is_open())
            {
                cerr << "Unable to open BED file: " << bedFile.Get() << endl;
                return 10;
            }
            //extract each line of the bed and insert it into the bedFeatures map
            while (extractBED(bedReader, line)) (*bedFeatures)[line.chromosome].push_back(line);
            bedReader.close();
        }

        //use boost to ensure that the output directory exists before the metrics are dumped to it
        if (!boost::filesystem::exists(outputDir.Get()))
        {
            boost::filesystem::create_directories(outputDir.Get());
        }

        BamReader bam;
        const string bamFilename = bamFile.Get();
        SamSequenceDictionary sequences; //for chromosome lookup
        Metrics counter; //main tracker for various metrics
        int readLength = 0; //longest read encountered so far
        map<string, double> geneCoverage, exonCoverage; //counters for read coverage of genes and exons
        BaseCoverage baseCoverage(outputDir.Get() + "/coverage.tmp.tsv");
        BiasCounter bias(BIAS_OFFSET, BIAS_WINDOW, BIAS_LENGTH);
        unsigned long long alignmentCount = 0ull; //count of how many alignments we've seen so far
        unsigned short current_chrom = 0;

        //Begin parsing the bam.  Each alignment is run through various sets of metrics
        {
            BamAlignment alignment; //current bam alignment
            time_t report_time; //used to ensure that stdout isn't spammed if the program runs super fast
            bam.Open(bamFilename);
            if (!bam.IsOpen())
            {
                cerr << "Unable to open BAM file: " << bamFilename << endl;
                return 10;
            }
            bam.LocateIndex(); //load in the index, if found.  Slightly improves IO perf
            sequences = bam.GetHeader().Sequences; //read the sequence dictionary from the header
            //Check the sequence dictionary for contig overlap with gtf
            if (VERBOSITY > 1) cout<<"Checking bam header..."<<endl;
            bool hasOverlap = false;
            for(auto sequence = sequences.Begin(); sequence != sequences.End(); ++sequence)
            {
                unsigned short chrom = chromosomeMap(sequence->Name);
                if (features.find(chrom) != features.end())
                {
                    hasOverlap = true;
                    break;
                }
            }
            if (!hasOverlap)
            {
                cerr << "BAM file shares no contigs with GTF" << endl;
                return 11;
            }
            if (VERBOSITY) cout<<"Parsing bam..."<<endl;
            time(&report_time);
            time(&t2);
            while (bam.GetNextAlignmentCore(alignment))
            {
                //try to print an update to stdout every 250,000 reads, but no more than once every 10 seconds
                ++alignmentCount;
                if (alignmentCount % 250000 == 0) time(&t2);
                if (difftime(t2, report_time) >= 10)
                {
                    time(&report_time);
                    if (VERBOSITY > 1) cout << "Time elapsed: " << difftime(t2, t1) << "; Alignments processed: " << alignmentCount << endl;
                }
                //count metrics based on basic read data

                if (!alignment.IsPrimaryAlignment()) counter.increment("Alternative Alignments");
                else if (alignment.IsFailedQC()) counter.increment("Failed Vendor QC");
                else if (alignment.MapQuality < LOW_QUALITY_READS_THRESHOLD) counter.increment("Low quality reads");
                if (alignment.IsPrimaryAlignment() && !alignment.IsFailedQC() /*&& alignment.MapQuality >= 255u*/)
                {
                    counter.increment("Unique Mapping, Vendor QC Passed Reads");
                    //raw counts:
                    if (!alignment.IsPaired()) counter.increment("Unpaired Reads");
                    if (alignment.IsDuplicate()) counter.increment("Duplicate Reads");
                    if (alignment.IsMapped())
                    {
                        counter.increment("Mapped Reads");

                        if (alignment.IsDuplicate())counter.increment("Mapped Duplicate Reads");
                        else counter.increment("Mapped Unique Reads");
                        //check length against max read length
                        unsigned int alignmentSize = alignment.GetEndPosition() - alignment.Position + 1;
                        if (alignmentSize > MAX_READ_LENGTH) continue;
                        if (!readLength) current_chrom = chromosomeMap((sequences.Begin()+alignment.RefID)->Name);
                        if (alignmentSize > readLength) readLength = alignment.Length;
                        alignment.BuildCharData(); //Load read name and tags
                        if (!LegacyMode.Get() && alignment.HasTag(chimeric_tag))
                        {
                            counter.increment("Chimeric Reads_tag");
                            if(excludeChimeric.Get()) continue;
                        }
                        if (alignment.IsPaired() && alignment.IsMateMapped() )
                        {
                            if (alignment.IsFirstMate()) counter.increment("Total Mapped Pairs");
                            if (alignment.RefID != alignment.MateRefID || abs(alignment.Position - alignment.MatePosition) > CHIMERIC_DISTANCE || (LegacyMode.Get() && alignment.RefID > 127))
                            {
                                counter.increment("Chimeric Reads_contig");
                                if(excludeChimeric.Get()) continue;
                            }
                        }
                        //Get tag data
                        unsigned int mismatches = 0;
                        if (alignment.HasTag(NM))
                        {
                            char nmType;
                            alignment.GetTagType(NM, nmType);
                            //The data type can vary based on the bam, but bamtools is strict about matching data types
                            //It is often platform-dependent whether or not a tag's data will fit properly into a different type
                            switch(nmType)
                            {
                                case Constants::BAM_TAG_TYPE_INT8:
                                    int8_t tmpi8;
                                    alignment.GetTag(NM, tmpi8);
                                    mismatches = (unsigned int) tmpi8;
                                    break;
                                case Constants::BAM_TAG_TYPE_UINT8:
                                    uint8_t tmpu8;
                                    alignment.GetTag(NM, tmpu8);
                                    mismatches = (unsigned int) tmpu8;
                                    break;
                                case Constants::BAM_TAG_TYPE_INT16:
                                    int16_t tmpi16;
                                    alignment.GetTag(NM, tmpi16);
                                    mismatches = (unsigned int) tmpi16;
                                    break;
                                case Constants::BAM_TAG_TYPE_UINT16:
                                    uint16_t tmpu16;
                                    alignment.GetTag(NM, tmpu16);
                                    mismatches = (unsigned int) tmpu16;
                                    break;
                                case Constants::BAM_TAG_TYPE_INT32:
                                    int32_t tmpi32;
                                    alignment.GetTag(NM, tmpi32);
                                    mismatches = (unsigned int) tmpi32;
                                    break;
                                case Constants::BAM_TAG_TYPE_UINT32:
                                    uint32_t tmpu32;
                                    alignment.GetTag(NM, tmpu32);
                                    mismatches = (unsigned int) tmpu32;
                                    break;
                                default:
                                    string msg = "";
                                    msg += nmType;
                                    throw std::invalid_argument("Unrecognized bam format: "+msg);
                            }

                            if (alignment.IsPaired())
                            {
                                if (alignment.IsFirstMate())
                                {
                                    counter.increment("End 1 Mapped Reads");
                                    counter.increment("End 1 Mismatches", mismatches);
                                    counter.increment("End 1 Bases", alignment.Length);
                                    if (alignment.IsDuplicate())counter.increment("Duplicate Pairs");
                                    else counter.increment("Unique Fragments");
                                }
                                else
                                {
                                    counter.increment("End 2 Mapped Reads");
                                    counter.increment("End 2 Mismatches", mismatches);
                                    counter.increment("End 2 Bases", alignment.Length);
                                }

                            }
                            counter.increment("Mismatched Bases", mismatches);
                        }
                        counter.increment("Total Bases", alignment.Length);
                        //generic filter tags:
                        bool discard = false;
                        for (auto tag = tags.begin(); tag != tags.end(); ++tag)
                        {
                            if (alignment.HasTag(tag->c_str()))
                            {
                                discard = true;
                                counter.increment("Filtered by tag: "+*tag);
                            }
                        }
                        if (discard) continue;

                        //now record intron/exon metrics by intersecting filtered reads with the list of features
                        if (alignment.RefID < 0 || alignment.RefID >= sequences.Size())
                        {
                            //The read had an unrecognized RefID (one not defined in the bam's header)
                            if (VERBOSITY) cerr << "Unrecognized RefID on alignment: " << alignment.Name<<endl;
                        }
                        else if(mismatches <= BASE_MISMATCH_THRESHOLD && (unpaired.Get() || alignment.IsProperPair()) && alignment.MapQuality >= MAPPING_QUALITY_THRESHOLD)
                        {
                            vector<Feature> blocks;
                            string chrName = (sequences.Begin()+alignment.RefID)->Name;
                            unsigned short chr = chromosomeMap(chrName); //parse out a chromosome shorthand
                            if (chr != current_chrom)
                            {
                                dropFeatures(features[current_chrom], baseCoverage);
                                current_chrom = chr;
                            }

                            //extract each cigar block from the alignment
                            unsigned int length = extractBlocks(alignment, blocks, chr, LegacyMode.Get());
                            trimFeatures(alignment, features[chr], baseCoverage); //drop features that appear before this read

                            //run the read through exon metrics
                            if (LegacyMode.Get()) legacyExonAlignmentMetrics(SPLIT_DISTANCE, features, counter, sequences, geneCoverage, exonCoverage, blocks, alignment, length, STRAND_SPECIFIC, bias, baseCoverage);
                            else exonAlignmentMetrics(SPLIT_DISTANCE, features, counter, sequences, geneCoverage, exonCoverage, blocks, alignment, length, STRAND_SPECIFIC, bias, baseCoverage);

                            //if fragment size calculations were requested, we still have samples to take, and the chromosome exists within the provided bed
                            if (doFragmentSize && alignment.IsPaired() && bedFeatures != nullptr && bedFeatures->find(chr) != bedFeatures->end())
                            {
                                doFragmentSize = fragmentSizeMetrics(doFragmentSize, bedFeatures, fragments, fragmentSizes, sequences, blocks, alignment);
                                if (!doFragmentSize && VERBOSITY > 1) cout << "Completed taking fragment size samples" << endl;
                            }
                        }
                        else counter.increment("Reads excluded from exon counts");
                    }

                }

            } //end of bam alignment loop
        } //end of bam alignment scope
        
        for (auto feats = features.begin(); feats != features.end(); ++feats)
            if (feats->second.size()) dropFeatures(feats->second, baseCoverage);

        baseCoverage.close();
        time(&t2);
        if (VERBOSITY)
        {
            cout<< "Time Elapsed: " << difftime(t2, t1) << "; Alignments processed: " << alignmentCount << endl;
            cout << "Total runtime: " << difftime(t2, t0) << "; Total CPU Time: " << (clock() - start_clock)/CLOCKS_PER_SEC << endl;
            if (VERBOSITY > 1) cout << "Average Reads/Sec: " << (double) alignmentCount / difftime(t2, t1) << endl;
            cout << "Estimating library complexity..." << endl;
        }
        counter.increment("Total Reads", alignmentCount);
        double duplicates = (double) counter.get("Duplicate Pairs");
        double unique = (double) counter.get("Unique Fragments");
        double numReads = duplicates + unique;
        unsigned int minReads = 0u, minError = UINT_MAX;
        if (duplicates > 0)
        {
            //If there are no duplicates, the estimate is useless, so skip it
            for (double x = unique; x < 1e9; ++x)
            {
                double estimate = x * (1.0 - exp(-1.0 * numReads / x)); //lander-waterman
                unsigned int error = (unsigned int) fabs(estimate - unique);
                if (error < minError)
                {
                    minError = error;
                    minReads = (unsigned int) x;
                }
            }
        }

        if (VERBOSITY) cout << "Generating report" << endl;

        //gene coverage report generation
        unsigned int genesDetected = 0;
        double fragmentMed = 0.0;
        vector<double> ratios;
        {
            ofstream geneReport(outputDir.Get()+"/"+SAMPLENAME+".gene_reads.gct");
            ofstream geneRPKM(outputDir.Get()+"/"+SAMPLENAME+".gene_"+(useRPKM.Get() ? "rpkm" : "tpm")+".gct");
            geneReport << "#1.2" << endl;
            geneRPKM << "#1.2" << endl;
            geneReport << geneList.size() << "\t1" << endl;
            geneRPKM << geneList.size() << "\t1" << endl;
            geneReport << "Name\tDescription\t" << (sampleName ? sampleName.Get() : "Counts") << endl;
            geneRPKM << "Name\tDescription\t" << (sampleName ? sampleName.Get() : (useRPKM.Get() ? "RPKM" : "TPM")) << endl;
            geneRPKM << fixed;
            const double scaleRPKM = (double) counter.get("Exonic Reads") / 1000000.0;
            double scaleTPM = 0.0;
//            vector<string> genesByRPKM;
            //iterate over every gene with coverage reported.  If it had at leat 5 reads, also count it as 'detected'
            //for(auto gene = geneCoverage.begin(); gene != geneCoverage.end(); ++gene)
            for(auto gene = geneList.begin(); gene != geneList.end(); ++gene)
            {
                geneReport << *gene << "\t" << geneNames[*gene] << "\t" << (long) geneCoverage[*gene] << endl;
                if (useRPKM.Get())
                {
                    double RPKM = (1000.0 * geneCoverage[*gene] / scaleRPKM) / (double) transcriptCodingLengths[*gene];
                    geneRPKM << *gene << "\t" << geneNames[*gene] << "\t" << RPKM << endl;
                }
                else
                {
                    double TPM = (1000.0 * geneCoverage[*gene]) / (double) transcriptCodingLengths[*gene];
                    tpms[*gene] = TPM;
                    scaleTPM += TPM;
                }
                if (geneCoverage[*gene] >= 5.0) ++genesDetected;
//                genesByRPKM.push_back(*gene);
                /*/
                if (gene->second * (double) readLength / (double) geneLengths[gene->first] > 1.0)
                {
                    double geneBias = bias.getBias(gene->first);
                    if (geneBias != -1.0) ratios.push_back(geneBias);
                }
                /*/
                //this gets you -.544, E-21
                //with partials, it's -.534, E-20
                double geneBias = bias.getBias(*gene);
                if (geneBias != -1.0) ratios.push_back(geneBias);
                /**/
            }
            geneReport.close();
            if (!useRPKM.Get())
            {
                scaleTPM /= 1000000.0;
                for(auto gene = geneList.begin(); gene != geneList.end(); ++gene)
                    geneRPKM << *gene << "\t" << geneNames[*gene] << "\t" << tpms[*gene] / scaleTPM << endl;
            }
            geneRPKM.close();

        }

        //3'/5' coverage ratio calculations
        double ratioAvg = 0.0, ratioMedDev, ratioMedian, ratioStd = 0.0, ratio75 = 0.0, ratio25 = 0.0;
        if (ratios.size())
        {
            vector<double> ratioDeviations;
            sort(ratios.begin(), ratios.end());
//            auto median = ratios.begin();
//            for (unsigned long midpoint = ratios.size() / 2; midpoint > 0; --midpoint) ++median;
//            ratioMedian = *median;
            ratioMedian = computeMedian(ratios.size(), ratios.begin());
            for (auto ratio = ratios.begin(); ratio != ratios.end(); ++ratio)
            {
                ratioAvg += (*ratio)/(double) ratios.size();
                ratioDeviations.push_back(fabs((*ratio) - ratioMedian));
            }
            sort(ratioDeviations.begin(), ratioDeviations.end());
            ratioMedDev = computeMedian(ratioDeviations.size(), ratioDeviations.begin()) * MAD_FACTOR;
//            ratioMedDev = ratioDeviations[ratioDeviations.size() /2] * MAD_FACTOR;
            for (auto ratio = ratios.begin(); ratio != ratios.end(); ++ratio)
            {
                ratioStd += pow((*ratio) - ratioAvg, 2.0) / (double) ratios.size();
            }
            ratioStd = pow(ratioStd, 0.5); //compute the standard deviation
            double index = .25 * ratios.size();
            if (index > floor(index))
            {
                index = ceil(index);
                ratio25 = ratios[(int) index];
            }
            else
            {
                index = ceil(index);
                ratio25 = (ratios[(int) index] + ratios[(int) index])/2.0;
            }
            index = .75 * ratios.size();
            if (index > floor(index))
            {
                index = ceil(index);
                ratio75 = ratios[(int) index];
            }
            else
            {
                index = ceil(index);
                ratio75 = (ratios[(int) index] + ratios[(int) index])/2.0;
            }
        }
        //exon coverage report generation
        {
            ofstream exonReport(outputDir.Get()+"/"+SAMPLENAME+".exon_reads.gct");
            exonReport << "#1.2" << endl;
            exonReport << exonCoverage.size() << "\t1" << endl;
            exonReport << "Name\tDescription\t" << (sampleName ? sampleName.Get() : "Counts") << endl;
            exonReport << fixed;
            //iterate over every exon with coverage reported
            //for(auto exon = exonCoverage.begin(); exon != exonCoverage.end(); ++exon)
            for(auto exon = exonList.begin(); exon != exonList.end(); ++exon)
            {
                exonReport << *exon << "\t" << geneNames[*exon] << "\t" << exonCoverage[*exon] << endl;
            }
            exonReport.close();
        }


        ofstream output(outputDir.Get()+"/"+SAMPLENAME+".metrics.tsv");
        //output rates and other fractions to the report
        output << "Sample\t" << SAMPLENAME << endl;
        output << "Mapping Rate\t" << counter.frac("Mapped Reads", "Total Reads") << endl;
        output << "Unique Rate of Mapped\t" << counter.frac("Mapped Unique Reads", "Mapped Reads") << endl;
        output << "Duplicate Rate of Mapped\t" << counter.frac("Mapped Duplicate Reads", "Mapped Reads") << endl;
        output << "Base Mismatch\t" << counter.frac("Mismatched Bases", "Total Bases") << endl;
        output << "End 1 Mapping Rate\t"<< 2.0 * counter.frac("End 1 Mapped Reads", "Total Reads") << endl;
        output << "End 2 Mapping Rate\t"<< 2.0 * counter.frac("End 2 Mapped Reads", "Total Reads") << endl;
        output << "End 1 Mismatch Rate\t" << counter.frac("End 1 Mismatches", "End 1 Bases") << endl;
        output << "End 2 Mismatch Rate\t" << counter.frac("End 2 Mismatches", "End 2 Bases") << endl;
        output << "Expression Profiling Efficiency\t" << counter.frac("Exonic Reads", "Total Reads") << endl;
        output << "Exonic Rate\t" << counter.frac("Exonic Reads", "Mapped Reads") << endl;
        output << "Intronic Rate\t" << counter.frac("Intronic Reads", "Mapped Reads") << endl;
        output << "Intergenic Rate\t" << counter.frac("Intergenic Reads", "Mapped Reads") << endl;
        output << "Intragenic Rate\t" << counter.frac("Intragenic Reads", "Mapped Reads") << endl;
        output << "Disqualification Rate\t" << counter.frac("Intron/Exon Disqualified Reads", "Mapped Reads") << endl;
        output << "Discard Rate\t" << (double)(counter.get("Mapped Reads") - counter.get("Reads used for Intron/Exon counts")) / counter.get("Mapped Reads") << endl;
        output << "rRNA Rate\t" << counter.frac("rRNA Reads", "Mapped Reads") << endl;
        output << "End 1 Sense Rate\t" << (double) counter.get("End 1 Sense") / (counter.get("End 1 Sense") + counter.get("End 1 Antisense")) << endl;
        output << "End 2 Sense Rate\t" << (double) counter.get("End 2 Sense") / (counter.get("End 2 Sense") + counter.get("End 2 Antisense")) << endl;
        output << "Avg. Splits per Read\t" << counter.frac("Alignment Blocks", "Mapped Reads") - 1.0 << endl;
        //automatically dump the raw counts of all metrics to the file
        output << counter;
        //append metrics that were manually tracked
        output << "Read Length\t" << readLength << endl;
        output << "Genes Detected\t" << genesDetected << endl;
        output << "Estimated Library Complexity\t" << minReads << endl;
        output << "Mean 3' bias\t" << ratioAvg << endl;
        output << "Median 3' bias\t" << ratioMedian << endl;
        //output << "Median 3' coverage\t" << _medianRatio2 << endl;
        output << "3' bias Std\t" << ratioStd << endl;
        output << "3' bias MAD_Std\t" << ratioMedDev << endl;
        output << "3' Bias, 25th Percentile\t" << ratio25 << endl;
        output << "3' Bias, 75th Percentile\t" << ratio75 << endl;

        if (fragmentSizes.size())
        {
            //If any fragment size samples were taken, also generate a fragment size report
            fragmentSizes.sort();



            double fragmentAvg = 0.0, fragmentStd = 0.0, fragmentMedDev = 0.0;
            //You may need to disable _GLIBCXX_USE_CXX11_ABI in order to compile this program, but that ends up
            //using the old implimentation of list which has to walk the entire sequence to determine size
            double size = (double) fragmentSizes.size();
            vector<double> deviations; //list of recorded deviations from the median
            fragmentMed = computeMedian(size, fragmentSizes.begin());
//            auto median = fragmentSizes.begin(); //reference the median value.  We have to walk the list to get here
//            for (int midpoint = size / 2; midpoint > 0; --midpoint) ++median;
//            fragmentMed = (double) *median; //save the median value
            ofstream fragmentList(outputDir.Get()+"/"+SAMPLENAME+".fragmentSizes.txt"); //raw list of each fragment size recorded
            for(auto fragment = fragmentSizes.begin(); fragment != fragmentSizes.end(); ++fragment)
            {
                fragmentList << abs(*fragment) << endl; //record the fragment size into the output list
                fragmentAvg += (double) abs(*fragment) / size; //add this fragment's size to the mean
                deviations.push_back(fabs((double) (*fragment) - fragmentMed)); //record this fragment's deviation
            }
            fragmentList.close();
            sort(deviations.begin(), deviations.end()); //for the next line to work, we have to sort
            //now compute the median absolute deviation, an estimator for standard deviation
//            fragmentMedDev = (double) deviations[deviations.size()/2] * MAD_FACTOR;
            fragmentMedDev = computeMedian(deviations.size(), deviations.begin()) * MAD_FACTOR;
            //we have to iterate again now for the standard deviation calculation, now that we know the mean
            for(auto fragment = fragmentSizes.begin(); fragment != fragmentSizes.end(); ++fragment)
            {
                fragmentStd += pow((double) (*fragment) - fragmentAvg, 2.0) / size;
            }
            fragmentStd = pow(fragmentStd, 0.5); //compute the standard deviation

            output << "Average Fragment Length\t" << fragmentAvg << endl;
            output << "Fragment Length Median\t" << fragmentMed << endl;
            output << "Fragment Length Std\t" << fragmentStd << endl;
            output << "Fragment Length MAD_Std\t" << fragmentMedDev << endl;
        }

        { //Do base-coverage metrics
            if (VERBOSITY) cout << "Computing per-base coverage metrics" << endl;
            list<double> means, stdDevs, cvs;
            ifstream reader(outputDir.Get() + "/coverage.tmp.tsv", ifstream::ate | ifstream::binary);
            double pos = reader.tellg();
            double nextUpdate = 0.1;
            reader.close();
            reader = ifstream(outputDir.Get() + "/coverage.tmp.tsv");
            ofstream writer(outputDir.Get() + "/" + SAMPLENAME + ".coverage.tsv");
            if (!outputTranscriptCoverage.Get()) writer.close(); //Close the writer preventing any output to the file
            writer << "gene_id\ttranscript_id\tcoverage_mean\t";
            writer << "coverage_std\tcoverage_CV" << endl;
            map<string, vector<CoverageEntry> > coverage;
            map<string, vector<string>> exons; //map of tid -> [exons]
            string line;
            while (getline(reader, line))
            {
                if ( VERBOSITY > 1 && ((double) reader.tellg()/pos) > nextUpdate)
                {
                    cout << round(((double) reader.tellg()/pos) * 100) << "% complete" << endl;
                    nextUpdate = ((double) reader.tellg()/pos) + 0.1;
                }
                std::istringstream tokenizer(line);
                string buffer, current_gene, current_transcript;
                getline(tokenizer, current_gene, '\t');
                getline(tokenizer, current_transcript, '\t');
                if (transcripts.find(current_transcript) == transcripts.end())
                {
                    cerr << "Current transcript " << current_transcript << " not present in GTF!" << endl;
                    continue;
                }
                Feature transcript = transcripts[current_transcript];

                auto elem = exons.begin();
                while (elem != exons.end())
                {
                    if (transcripts.find(elem->first) == transcripts.end())
                    {
                        cerr << "Unknown transcript in buffer: " << elem->first << endl;
                    }
                    Feature compTranscript = transcripts[elem->first];
                    if (transcript.chromosome != compTranscript.chromosome || transcript.start > compTranscript.end)
                    {
                        auto results = computeCoverage(writer, compTranscript.gene_id, compTranscript.feature_id, fragmentMed, coverage, elem->second);
                        means.push_back(get<0>(results));
                        stdDevs.push_back(get<1>(results));
                        cvs.push_back(get<2>(results));
                        for (auto exon = elem->second.begin(); exon != elem->second.end(); ++exon) coverage.erase(*exon);
                        exons.erase(elem++);
                    }
                    else ++elem;
                }
                CoverageEntry tmp;
                tmp.transcript_id = transcript.feature_id;
                getline(tokenizer, tmp.feature_id, '\t');
                getline(tokenizer, buffer, '\t');
                tmp.offset = stoull(buffer);
                getline(tokenizer, buffer, '\t');
                tmp.length = stoul(buffer);
                if (!(exons[transcript.feature_id].size() && exons[transcript.feature_id].back() == tmp.feature_id)) exons[transcript.feature_id].push_back(tmp.feature_id);
                coverage[tmp.feature_id].push_back(tmp);
            }
            if (exons.size())
            {
                for (auto elem = exons.begin(); elem != exons.end(); ++elem)
                {
                    Feature transcript = transcripts[elem->first];
                    if (elem->second.size())
                    {
                        auto results = computeCoverage(writer, transcript.gene_id, transcript.feature_id, fragmentMed, coverage, elem->second);
                        means.push_back(get<0>(results));
                        stdDevs.push_back(get<1>(results));
                        cvs.push_back(get<2>(results));
                        for (auto exon = elem->second.begin(); exon != elem->second.end(); ++exon) coverage.erase(*exon);
                    }
                    else ++elem;
                }
                if (coverage.size()) cout << "WARNING: " << coverage.size() << " unclaimed exons" << endl;
            }
            writer.close();
            remove(string(outputDir.Get() + "/coverage.tmp.tsv").c_str());
            if (!outputTranscriptCoverage.Get()) remove(string(outputDir.Get() + "/" + SAMPLENAME + ".coverage.tsv").c_str());
            if (VERBOSITY > 1) cout << "Computing median coverage statistics" << endl;
            unsigned long nTranscripts = means.size();
            means.sort();
            stdDevs.sort();
            auto beg = cvs.begin();
            auto end = cvs.end();
            while (beg != end)
            {
                if (std::isnan(*beg) || std::isinf(*beg)) cvs.erase(beg++);
                else ++beg;
            }
            cvs.sort();
            output << "Median of Avg Transcript Coverage\t" << computeMedian(nTranscripts, means.begin()) << endl;
            output << "Median of Transcript Coverage Std\t" << computeMedian(nTranscripts, stdDevs.begin()) << endl;
            output << "Median of Transcript Coverage CV\t" << computeMedian(cvs.size(), cvs.begin()) << endl;
        }

        output.close();

	}
    catch (args::Help)
    {
        cout << parser;
        return 4;
    }
    catch (args::ParseError &e)
    {
        cerr << parser << endl;
        cerr << "Argument parsing error: " << e.what() << endl;
        return 5;
    }
    catch (args::ValidationError &e)
    {
        cerr << parser << endl;
        cerr << "Argument validation error: " << e.what() << endl;
        return 6;
    }
    catch (std::invalid_argument &e)
    {
        cerr << "Invalid argument type provided: " << e.what() << endl;
        return 7;
    }
    catch (boost::filesystem::filesystem_error &e)
    {
        cerr << "Filesystem error:  " << e.what() << endl;
        return 8;
    }
    catch (std::length_error &e)
    {
        cerr<<"Unable to parse the GFT lines"<<endl;
        cerr<<e.what()<<endl;
        return 1;
    }
    catch (std::range_error &e)
    {
        cerr<<"Invalid chromosome range"<<endl;
        cerr<<e.what()<<endl;
        return 2;
    }
    catch(std::domain_error &e)
    {
        cerr<<"Unable to perform string conversion"<<endl;
        cerr<<e.what()<<endl;
        return 3;
    }
	catch (...)
	{
        cerr << parser << endl;
        cerr << "Unknown error" << endl;
        return -1;
	}

    return 0;
}

void add_range(vector<unsigned long> &coverage, coord offset, unsigned int length)
{
//    if (offset + length >= coverage.size()) coverage.resize(offset + length, 0ul);
//    for (coord i = offset; offset < offset + length; ++i) coverage[i] = coverage[i] + 1;
    for (coord i = 0; i < offset + length; ++i)
    {
        unsigned long x = i >= offset ? 1ul : 0ul;
        if (i >= coverage.size()) coverage.push_back(x);
        else coverage[i] = coverage[i] + x;
    }
}

tuple<double, double, double> computeCoverage(ofstream &writer, string &gene_id, string &transcript_id, const double median_insert_size, map<string, vector<CoverageEntry> > &entries, vector<string> &exons)
{
    vector<unsigned long> coverage;
    for (unsigned int i = 0; i < exons.size(); ++i)
    {
        auto beg = entries[exons[i]].begin();
        auto end = entries[exons[i]].end();
        vector<unsigned long> exon_coverage;
        while (beg != end)
        {
            add_range(exon_coverage, beg->offset, beg->length);
            ++beg;
        }
        coverage.reserve(coverage.size() + exon_coverage.size());
        coverage.insert(coverage.end(), exon_coverage.begin(), exon_coverage.end());
    }
    double avg = 0.0, std = 0.0;
//    auto median = coverage.begin();
    double size = (double) coverage.size() - (2 * median_insert_size);
    if (size > 0)
    {
        unsigned int i = 0u;
        for (auto beg = coverage.begin(); beg != coverage.end() && i < size; ++beg, ++i)
            avg += (double) (*beg) / size;
        i = 0u;
        for (auto base = coverage.begin(); base != coverage.end() && i < size; ++base, ++i)
            std += pow((double) (*base) - avg, 2.0) / size;
        std = pow(std, 0.5);
        writer << gene_id << "\t" << transcript_id << "\t";
        writer << avg << "\t" << std << "\t" << (std / avg) << endl;
    }
    return make_tuple(avg, std, (std / avg));
}

template <typename T>
double computeMedian(unsigned long size, T &&iterator, unsigned int offset)
{
    for (unsigned long midpoint = size / 2; midpoint > offset; --midpoint) ++iterator;
    if (size % 1)
    {
        double value = (double) (*(iterator++));
        return (value + (double) (*iterator)) / 2.0;
    }
    return (double) (*iterator);
}

bool compGenes(const string &a, const string &b)
{
    return tpms[a] < tpms[b];
}
