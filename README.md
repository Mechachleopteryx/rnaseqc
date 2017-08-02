# RNA-SeQC
Version 2.0.0

---

### Command Line Usage:

`rnaseqc [OPTIONS] gtf bam output`

###### OPTIONS:
      -h, --help                        Display this message and quit

      gtf                               The input GTF file containing features
                                        to check the bam against

      bam                               The input SAM/BAM file containing reads
                                        to process

      output                            Output directory

      --bed=[BEDFILE]                   Optional input BED file containing
                                        non-overlapping exons used for fragment
                                        size calculations

      --chimeric-distance=[DISTANCE]    Set the maximum accepted distance
                                        between read mates. Mates beyond this
                                        distance will be counted as chimeric
                                        pairs. Default: 2Mb

      --read-length=[LENGTH]            Set the maximum accepted length. Reads
                                        longer than this threshold are
                                        discarded. Default: 100Kb

      --samples=[SAMPLES]               Set the number of samples to take when
                                        computing fragment sizes. Requires the
                                        --bed argument. Default: 1,000,000

      --low-quality=[QUALITY]           Set the lower bound on read quality.
                                        Reads below this number are counted as
                                        low quality BUT ARE STILL USED IN
                                        COUNTS. See --exon-quality to discard
                                        reads based on quality. Default: 255

      --exon-quality=[QUALITY]          Set the lower bound on read quality for
                                        exon coverage counting. Reads below this
                                        number are excluded from coverage
                                        metrics. Default: 255

      --exon-mismatch=[MISMATCHES]      Set the maximum number of allowed
                                        mismatches between a read and the
                                        reference sequence. Reads with more than
                                        this number of mismatches are excluded
                                        from coverage metrics. Default: 6

      --split-distance=[DISTANCE]       Set the maximum distance between aligned
                                        blocks of a read. Reads with aligned
                                        blocks separated by more than this
                                        distance are counted as split reads, BUT
                                        ARE STILL USED IN COUNTS. Default: 100bp

      -d, --debug                       Include values of various internal
                                        constants in the output

      --legacy                          Use legacy gene counting rules. Gene
                                        counts match output of RNA-SeQC 1.1.6

      -v, --verbose                     Give some feedback about what's going
                                        on. Supply this argument twice for
                                        progress updates while parsing the bam

      "--" can be used to terminate flag options and force all following
      arguments to be treated as positional options

### Output files:
The following output files are generated in the output directory you provide:
* report.tsv : A tab-delimited list of (Statistic, Value) pairs of all statistics and metrics recorded.
* exonReport.tsv : A tab-delimited list of (Exon ID, coverage) pairs for all exons which had at least part of one read mapped
* geneReport.tsv : A tab-delimited list of (Gene ID, coverage) pairs for all exons which had at least one read map to at least one of its exons
* fragmentSizes.txt : A list of fragment sizes recorded, if a BED file was provided
      
### Code Documentation:

###### Included source files:
* RNASeQC.cpp : Main entry point for the application
* Expression.cpp : Function definitions for running Exon and Gene expression metrics (the bulk of the program)
* Expression.h : Function declarations for Expression.h
* Metrics.cpp : Function definitions for the `Collector` and `Metrics` classes
* Metrics.h : `Collector` and `Metrics` class declarations
* GTF.cpp : Function definitions for parsing GTF files and for `Feature` class functions
* GTF.h : `Feature` class and related function declarations
* BED.cpp : `extractBED()` function definition
* BED.h : Declares `extractBED()`

#### Documentation

##### RNASeQC
* `int main(int, char**)` : Parses command line arguments then runs RNASeQC.  First it reads and parses the GTF into lists of features (one list per contig).  This accounts for the bulk of the memory overhead.  If a BED file was provided, it will also parse it and store it in similar lists.  Then it iterates over each read in the BAM file, and counts various attributes of the reads.  Reads which pass filtering (must be a primary alignment, be mapped, have an overall read span less than the maximum read length, be in a proper pair, have a mapping quality >= the quality threshold, have no more than the maximum number of mismatches, and cannot fail vendor QC) are run through exon coverage metrics (see Expression.cpp).  If a BED file was provided, each read which passes the same filtering steps is also run through fragment size metrics.  After all reads have been processed, some statistics are caluclated and then a report is generated

##### Expression
* `template <typename T> bool isIn(std::set<T> &, T)` : Utility function to determine if an object belongs to a set

* `unsigned int extractBlocks(BamTools::BamAlignment&, std::vector<Feature>&, unsigned short);` : Function to extract the aligned segments of a read into separate objects for querying the GTF

* `std::list<Feature>* intersectBlock(Feature&, std::list<Feature>&);` : Function to query one read segment against the list of GTF features.  **Note: The returned list is dynamically allocated and must be freed**

* `void trimFeatures(BamTools::BamAlignment&, std::list<Feature> &);` : Function to trim out features from the GTF which will not be queried again. This helps to speed up the program as it advances down each contig

* `unsigned int fragmentSizeMetrics(<args...>)` : Queries each segment of a read against the intervals in the BED file.  If all segments of a read as well as all segments of the read's mate map to the same interval, the pair is deemed 'valid' and its insert size is recorded in the list of fragment sizes

* `void exonAlignmentMetrics(<args...>)` : Queries each segment of a read against the features in the GTF. To be counted towards a gene, all segments of a read must fully map to exons of the gene. Exon and gene coverage is discarded for reads which do not meet that criteria (having bases overhang off an exon/gene, aligning to introns or intergenic regions, etc). Reads are counted as Intronic if none of their segments aligned to any exons (even if the coverage is discarded), and Intergenic if they additionally never intersected any genes. Because of this criteria, a read is counted as disqualified if not all of its segments fully mapped to exons of the same gene(s).

* `void legacyExonAlignmentMetrics(<args...>)` : Modified version of `exonAlignmentMetrics()`. This version was written to exactly match the gene expression counts (geneReport.tsv) from RNA-SeQC 1.1.6.  Other metrics will also match closer to the old version.  Note that these counts are not 100% accurate due to bugs that were intentionally included in the legacy version of this function.

##### Metrics
* `Metrics` (class) : Used for keeping track of various counters and statistics regarding the reads that have been processed so far

* `Collector` (class) : Used to collect exon coverage per gene, per read. Only exons belonging to genes that are not disqualified (all segments of a read map to an exon of the same gene) will be kept.

* `std::ofstream& operator<<(std::ofstream&, const Metrics&)` : Dumps all the raw counts from the `Metrics` counter to the output stream

##### GTF
* `coord` (typedef) : `long long`

* `Feature` (struct) : Intended to represent GTF features. However, throughout the program it's used to represent any type of genomic interval

* `bool operator==(const Feature &a, const Feature &b)` : returns true if the two features have the same start, end, chromosome, strand, type, feature id, and transcript id

* `bool compIntervalStart(const Feature&, const Feature&)` : returns true if the first feature starts before the second

* `bool compIntervalEnd(const Feature&, const Feature&)` : returns true if the first feature ends before the second

* `bool intersectPoint(const Feature&, const coord)` : returns true if the coordinate falls within the feature's interval

* `bool intersectInterval(const Feature&, const Feature&)` : returns true if the two intervals have any overlap

* `int partialIntersect(const Feature&, const Feature&)` : returns the exact size of the overlap between two features

* `std::map<std::string, unsigned short> chromosomes` (external variable) : Externally available map of chromosomes to their internal ID.  IDs are arbitrarily assigned (and are used in place of strings for performance reasons)

* `unsigned short chromosomeMap(std::string)` : returns the internal ID for the provided chromosome name

* `std::ifstream& operator>>(std::ifstream&, Feature&)` : reads from the ifstream (assumes GTF format).  Stores the next feature encountered in the provided reference

* `std::map<std::string,std::string>& parseAttributes(std::string&, std::map<std::string,std::string>&)` : Parses the attribute string of a GTF feature into a map

##### BED
* `std::ifstream& extractBED(std::ifstream&, Feature&)` : reads from the ifstream (assumes BED format). Stores the next feature encountered in the provided reference
