// -*- mode: c++; indent-tabs-mode: nil; -*-
//
// Starka
// Copyright (c) 2009-2014 Illumina, Inc.
//
// This software is provided under the terms and conditions of the
// Illumina Open Source Software License 1.
//
// You should have received a copy of the Illumina Open Source
// Software License 1 along with this program. If not, see
// <https://github.com/sequencing/licenses/>
//

/// \author Chris Saunders
///

#include "denovo_indel_call_vcf.hh"
#include "inovo_vcf_locus_info.hh"
#include "blt_util/blt_exception.hh"
#include "blt_util/io_util.hh"

#include <iomanip>
#include <iostream>



static
void
write_vcf_sample_info(
    const starling_indel_sample_report_info& isri1,
    const starling_indel_sample_report_info& isri2,
    std::ostream& os)
{
    static const char sep(':');
    os << isri1.depth
       << sep
       << isri2.depth
       << sep
       << isri1.n_q30_ref_reads+isri1.n_q30_alt_reads << ','
       << isri2.n_q30_ref_reads+isri2.n_q30_alt_reads
       << sep
       << isri1.n_q30_indel_reads << ','
       << isri2.n_q30_indel_reads
       << sep
       << isri1.n_other_reads << ','
       << isri2.n_other_reads;
}


#if 0
static
double
safeFrac(const int num, const int denom)
{
    return ( (denom > 0) ? (num/static_cast<double>(denom)) : 0.);
}
#endif



void
denovo_indel_call_vcf(
    const inovo_options& opt,
    const inovo_deriv_options& dopt,
    const SampleInfoManager& sinfo,
    const denovo_indel_call& dinc,
    const starling_indel_report_info& iri,
    const std::vector<isriTiers_t>& isri,
    std::ostream& os)
{
    const denovo_indel_call::result_set& rs(dinc.rs);

    inovo_shared_modifiers smod;
    {
        // compute all site filters:
        if (dopt.dfilter.is_max_depth())
        {
            using namespace INOVO_SAMPLETYPE;
            const unsigned probandIndex(sinfo.getTypeIndexList(PROBAND)[0]);

            const unsigned& depth(isri[probandIndex][INOVO_TIERS::TIER1].depth);
            if (depth > dopt.dfilter.max_depth)
            {
                smod.set_filter(INOVO_VCF_FILTERS::HighDepth);
            }
        }

        if (rs.dindel_qphred < opt.dfilter.dindel_qual_lowerbound)
        {
            smod.set_filter(INOVO_VCF_FILTERS::QDI);
        }

#if 0
        if (siInfo.iri.ref_repeat_count > opt.dfilter.indelMaxRefRepeat)
        {
            smod.set_filter(INOVO_VCF_FILTERS::Repeat);
        }

        if (siInfo.iri.ihpol > opt.dfilter.indelMaxIntHpolLength)
        {
            smod.set_filter(INOOV_VCF_FILTERS::iHpol);
        }
#endif

#if 0
        {
            const int normalFilt(wasNormal.ss_filt_win.avg());
            const int normalUsed(wasNormal.ss_used_win.avg());
            const float normalWinFrac(safeFrac(normalFilt,(normalFilt+normalUsed)));

            const int tumorFilt(wasTumor.ss_filt_win.avg());
            const int tumorUsed(wasTumor.ss_used_win.avg());
            const float tumorWinFrac(safeFrac(tumorFilt,(tumorFilt+tumorUsed)));

            if ((normalWinFrac >= opt.sfilter.indelMaxWindowFilteredBasecallFrac) ||
                (tumorWinFrac >= opt.sfilter.indelMaxWindowFilteredBasecallFrac))
            {
                smod.set_filter(STRELKA_VCF_FILTERS::IndelBCNoise);
            }
        }

        if ((rs.ntype != NTYPE::REF) || (rs.sindel_from_ntype_qphred < opt.sfilter.sindelQuality_LowerBound))
        {
            smod.set_filter(STRELKA_VCF_FILTERS::QSI_ref);
        }
#endif
    }


    static const char sep('\t');

    // REF/ALT
    os << sep << iri.vcf_ref_seq
       << sep << iri.vcf_indel_seq;

    //QUAL:
    os << sep << ".";

    //FILTER:
    os << sep;
    smod.write_filters(os);

    //INFO
    unsigned totalDepth(0);
    for (const auto& sampleIsri : isri)
    {
        totalDepth += sampleIsri[INOVO_TIERS::TIER1].depth;
    }

    os << sep
       << "DP="<< totalDepth
       << ";QDI=" << rs.dindel_qphred
       << ";TQDI=" << (dinc.dindel_tier+1)
;

    if (iri.is_repeat_unit())
    {
        os << ";RU=" << iri.repeat_unit
           << ";RC=" << iri.ref_repeat_count
           << ";IC=" << iri.indel_repeat_count;
    }
    os << ";IHP=" << iri.ihpol;
    if ((iri.it == INDEL::BP_LEFT) ||
        (iri.it == INDEL::BP_RIGHT))
    {
        os << ";SVTYPE=BND";
    }
    if (rs.is_overlap)
    {
        os << ";OVERLAP";
    }

    //FORMAT
    os << sep << "DP:DP2:TAR:TIR:TOR";

    // write sample info:
    for (const auto& sampleIsri : isri)
    {
        os << sep;
        write_vcf_sample_info(sampleIsri[INOVO_TIERS::TIER1],sampleIsri[INOVO_TIERS::TIER2], os);
    }
}