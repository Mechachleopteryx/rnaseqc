#Set inclusion paths here (if boost, bamtools, or args are installed outside your path)
INCLUDE_DIRS=-I/usr/local/include/bamtools
#Set library paths here (if boost or bamtools are installed outside your path)
LIBRARY_PATHS=
#Set to 0 if you encounter linker errors regarding strings from the bamtools library
ABI=1
#Provide full paths here to .a archives for libraries which should be statically linked
STATIC_LIBS=
#List of remaining libraries that will be dynamically linked
LIBS=-lbamtools -lboost_filesystem -lboost_regex -lboost_system -lz

CC=g++
STDLIB=-std=c++14
CFLAGS=-Wall $(STDLIB) -D_GLIBCXX_USE_CXX11_ABI=$(ABI) -O3
SOURCES=BED.cpp Expression.cpp GTF.cpp RNASeQC.cpp Metrics.cpp Fasta.cpp
SRCDIR=src
OBJECTS=$(SOURCES:.cpp=.o)

rnaseqc: $(foreach file,$(OBJECTS),$(SRCDIR)/$(file))
	$(CC) -O3 $(LIBRARY_PATHS) -o $@ $^ $(STATIC_LIBS) $(LIBS)

%.o: %.cpp
	$(CC) $(CFLAGS) -I. $(INCLUDE_DIRS) -c -o $@ $<

.PHONY: clean

clean:
	rm $(wildcard $(SRCDIR)/*.o)

# The rest of the makefile consists of test cases. Run "make test" to perform all tests

.PHONY: test

test: test-version test-single test-chr1 test-downsampled test-legacy test-expected-failures
	echo Tests Complete

.PHONY: test-version

test-version: rnaseqc
	[ ! -z "$(shell ./rnaseqc --version)" ]

.PHONY: test-single

test-single: rnaseqc
	./rnaseqc test_data/single_pair.gtf test_data/single_pair.bam .test_output
	diff .test_output/single_pair.bam.metrics.tsv test_data/single_pair.output/single_pair.bam.metrics.tsv
	diff .test_output/single_pair.bam.gene_reads.gct test_data/single_pair.output/single_pair.bam.gene_reads.gct
	diff .test_output/single_pair.bam.gene_tpm.gct test_data/single_pair.output/single_pair.bam.gene_tpm.gct
	diff .test_output/single_pair.bam.exon_reads.gct test_data/single_pair.output/single_pair.bam.exon_reads.gct
	rm -rf .test_output

.PHONY: test-chr1

test-chr1: rnaseqc
	./rnaseqc test_data/chr1.gtf test_data/chr1.bam .test_output --coverage
	diff .test_output/chr1.bam.metrics.tsv test_data/chr1.output/chr1.bam.metrics.tsv
	diff .test_output/chr1.bam.gene_reads.gct test_data/chr1.output/chr1.bam.gene_reads.gct
	diff .test_output/chr1.bam.gene_tpm.gct test_data/chr1.output/chr1.bam.gene_tpm.gct
	diff .test_output/chr1.bam.exon_reads.gct test_data/chr1.output/chr1.bam.exon_reads.gct
	sed s/-nan/nan/g .test_output/chr1.bam.coverage.tsv > .test_output/coverage.tsv
	diff .test_output/coverage.tsv test_data/chr1.output/chr1.bam.coverage.tsv
	rm -rf .test_output

.PHONY: test-downsampled

test-downsampled: rnaseqc
	./rnaseqc test_data/downsampled.gtf test_data/downsampled.bam --bed test_data/downsampled.bed --coverage .test_output
	diff .test_output/downsampled.bam.metrics.tsv test_data/downsampled.output/downsampled.bam.metrics.tsv
	diff .test_output/downsampled.bam.gene_reads.gct test_data/downsampled.output/downsampled.bam.gene_reads.gct
	diff .test_output/downsampled.bam.gene_tpm.gct test_data/downsampled.output/downsampled.bam.gene_tpm.gct
	diff .test_output/downsampled.bam.exon_reads.gct test_data/downsampled.output/downsampled.bam.exon_reads.gct
	sed s/-nan/nan/g .test_output/downsampled.bam.coverage.tsv > .test_output/coverage.tsv
	diff .test_output/coverage.tsv test_data/downsampled.output/downsampled.bam.coverage.tsv
	diff .test_output/downsampled.bam.fragmentSizes.txt test_data/downsampled.output/downsampled.bam.fragmentSizes.txt
	rm -rf .test_output

.PHONY: test-legacy

test-legacy: rnaseqc
	./rnaseqc test_data/downsampled.gtf test_data/downsampled.bam --bed test_data/downsampled.bed --coverage .test_output --legacy
	diff .test_output/downsampled.bam.metrics.tsv test_data/legacy.output/downsampled.bam.metrics.tsv
	diff .test_output/downsampled.bam.gene_reads.gct test_data/legacy.output/downsampled.bam.gene_reads.gct
	diff .test_output/downsampled.bam.gene_tpm.gct test_data/legacy.output/downsampled.bam.gene_tpm.gct
	diff .test_output/downsampled.bam.exon_reads.gct test_data/legacy.output/downsampled.bam.exon_reads.gct
	sed s/-nan/nan/g .test_output/downsampled.bam.coverage.tsv > .test_output/coverage.tsv
	diff .test_output/coverage.tsv test_data/legacy.output/downsampled.bam.coverage.tsv
	diff .test_output/downsampled.bam.fragmentSizes.txt test_data/legacy.output/downsampled.bam.fragmentSizes.txt
	python3 test_data/legacy_test.py .test_output/downsampled.bam.gene_reads.gct test_data/legacy.output/legacy.gene_reads.gct
	python3 python/legacy_exon_remap.py .test_output/downsampled.bam.exon_reads.gct test_data/downsampled.gtf > /dev/null
	python3 test_data/legacy_test.py .test_output/downsampled.bam.exon_reads.gct test_data/legacy.output/legacy.exon_reads.gct --approx
	rm -rf .test_output

.PHONY: test-expected-failures

test-expected-failures: rnaseqc
	./rnaseqc test_data/gencode.v26.collapsed.gtf test_data/downsampled.bam .test_output 2>/dev/null; test $$? -eq 11
	rm -rf .test_output
