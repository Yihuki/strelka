// -*- mode: c++; indent-tabs-mode: nil; -*-
//
// Strelka - Small Variant Caller
// Copyright (c) 2009-2016 Illumina, Inc.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//

/*
 * \author Morten Kallberg
 */

#include "ScoringModelManager.hh"


//#define DEBUG_CAL

#ifdef DEBUG_CAL
#include "blt_util/log.hh"
#endif



ScoringModelManager::
ScoringModelManager(
    const starling_options& opt,
    const gvcf_deriv_options& gvcfDerivedOptions)
    : _opt(opt.gvcf),
      _dopt(gvcfDerivedOptions),
      _isReportEVSFeatures(opt.isReportEVSFeatures),
      _isRNA(opt.isRNA)
{
    if (opt.isReportEVSFeatures)
    {
        /// EVS feature output is contrained to the single-sample input case right now:
        const unsigned sampleCount(opt.alignFileOpt.alignmentFilename.size());
        assert(1 == sampleCount);
    }

    const SCORING_CALL_TYPE::index_t callType(opt.isRNA ? SCORING_CALL_TYPE::RNA : SCORING_CALL_TYPE::GERMLINE);

    if (not opt.snv_scoring_model_filename.empty())
    {
        _snvScoringModelPtr.reset(
            new VariantScoringModelServer(
                _dopt.snvFeatureSet.getFeatureMap(),
                opt.snv_scoring_model_filename,
                callType,
                SCORING_VARIANT_TYPE::SNV)
        );
    }

    if (not opt.indel_scoring_model_filename.empty())
    {
        _indelScoringModelPtr.reset(
            new VariantScoringModelServer(
                _dopt.indelFeatureSet.getFeatureMap(),
                opt.indel_scoring_model_filename,
                callType,
                SCORING_VARIANT_TYPE::INDEL)
        );
    }
}



void
ScoringModelManager::
classify_site(
    GermlineDiploidSiteLocusInfo& locus) const
{
    /// locus must have at least one variant
    /// TODO STREL-125 fix for multi-sample
    const bool isVariantUsableInEVSModel(locus.isVariantLocus());

    const unsigned sampleCount(locus.getSampleCount());
    if (isVariantUsableInEVSModel && _isReportEVSFeatures)
    {
        assert(sampleCount == 1);
        const unsigned sampleIndex(0);

        // when reporting is turned on, we need to compute EVS features
        // for any usable variant regardless of EVS model type:
        const bool isUniformDepthExpected(_dopt.is_max_depth());
        GermlineDiploidSiteLocusInfo::computeEmpiricalScoringFeatures(
            locus, sampleIndex, _isRNA, isUniformDepthExpected, _isReportEVSFeatures,
            _dopt.norm_depth, locus.evsFeatures, locus.evsDevelopmentFeatures);
    }

    if (isVariantUsableInEVSModel && isEVSSiteModel())
    {
        const unsigned allSampleLocusDepth(locus.getTotalReadDepth());
        for (unsigned sampleIndex(0); sampleIndex < sampleCount; ++sampleIndex)
        {
            /// TODO STREL-125 fix for multi-sample:
            auto& sampleInfo(locus.getSample(sampleIndex));
            if (not sampleInfo.isVariant())
            {
                // revert to hard-filters for this sample:
                default_classify_site(sampleIndex, allSampleLocusDepth, locus);
                continue;
            }

            static const bool isComputeDevelopmentFeatures(false);
            const bool isUniformDepthExpected(_dopt.is_max_depth());
            if (not _isReportEVSFeatures)
            {
                GermlineDiploidSiteLocusInfo::computeEmpiricalScoringFeatures(
                    locus, sampleIndex, _isRNA, isUniformDepthExpected, isComputeDevelopmentFeatures,
                    _dopt.norm_depth, locus.evsFeatures, locus.evsDevelopmentFeatures);
            }

            static const int maxEmpiricalVariantScore(60);
            sampleInfo.empiricalVariantScore = std::min(
                error_prob_to_qphred(_snvScoringModelPtr->scoreVariant(locus.evsFeatures.getAll())),
                maxEmpiricalVariantScore);

            if (sampleInfo.empiricalVariantScore < snvEVSThreshold())
            {
                sampleInfo.filters.set(GERMLINE_VARIANT_VCF_FILTERS::LowGQX);
            }
        }
    }
    else
    {
        // don't know what to do with this site, throw it to the old default filters
        default_classify_site_locus(locus);
    }
}



void
ScoringModelManager::
classify_indel_impl(
    GermlineDiploidIndelLocusInfo& locus) const
{
    /// locus must have at least one variant and no breakpoints
    const bool isVariantUsableInEVSModel(locus.isVariantLocus() and (not locus.isAnyBreakpointAlleles()));

    const unsigned sampleCount(locus.getSampleCount());
    if (isVariantUsableInEVSModel && _isReportEVSFeatures)
    {
        assert(sampleCount == 1);
        const unsigned sampleIndex(0);

        // when reporting is turned on, we need to compute EVS features
        // for any usable variant regardless of EVS model type:
        const bool isUniformDepthExpected(_dopt.is_max_depth());
        GermlineDiploidIndelLocusInfo::computeEmpiricalScoringFeatures(
            locus, sampleIndex, _isRNA, isUniformDepthExpected,_isReportEVSFeatures,
            _dopt.norm_depth, locus.evsFeatures, locus.evsDevelopmentFeatures);
    }

    if (isVariantUsableInEVSModel && isEVSIndelModel())
    {
        const unsigned allSampleLocusDepth(locus.getTotalReadDepth());
        for (unsigned sampleIndex(0); sampleIndex < sampleCount; ++sampleIndex)
        {
            auto& sampleInfo(locus.getSample(sampleIndex));
            if (not sampleInfo.isVariant())
            {
                // revert to hard-filters for this sample:
                default_classify_indel(sampleIndex, allSampleLocusDepth, locus);
                continue;
            }

            static const bool isComputeDevelopmentFeatures(false);
            const bool isUniformDepthExpected(_dopt.is_max_depth());
            if (not _isReportEVSFeatures)
            {
                GermlineDiploidIndelLocusInfo::computeEmpiricalScoringFeatures(
                    locus, sampleIndex, _isRNA, isUniformDepthExpected, isComputeDevelopmentFeatures,
                    _dopt.norm_depth, locus.evsFeatures, locus.evsDevelopmentFeatures);
            }

            static const int maxEmpiricalVariantScore(60);
            sampleInfo.empiricalVariantScore = std::min(
                error_prob_to_qphred(_indelScoringModelPtr->scoreVariant(locus.evsFeatures.getAll())),
                maxEmpiricalVariantScore);

            if (sampleInfo.empiricalVariantScore < indelEVSThreshold())
            {
                sampleInfo.filters.set(GERMLINE_VARIANT_VCF_FILTERS::LowGQX);
            }
        }
    }
    else
    {
        default_classify_indel_locus(locus);
    }
}



void
ScoringModelManager::
classify_indel(
    GermlineDiploidIndelLocusInfo& locus) const
{
    classify_indel_impl(locus);
}



void
ScoringModelManager::
default_classify_site(
    const unsigned sampleIndex,
    const unsigned allSampleLocusDepth,
    GermlineSiteLocusInfo& locus) const
{
    LocusSampleInfo& sampleInfo(locus.getSample(sampleIndex));

    if (_opt.is_min_gqx)
    {
        if (sampleInfo.gqx < _opt.min_gqx) sampleInfo.filters.set(GERMLINE_VARIANT_VCF_FILTERS::LowGQX);
    }
    if (_dopt.is_max_depth())
    {
        if (allSampleLocusDepth > _dopt.max_depth)
            sampleInfo.filters.set(GERMLINE_VARIANT_VCF_FILTERS::HighDepth);
    }

    // high DPFratio filter
    if (_opt.is_max_base_filt)
    {
        const auto& siteSampleInfo(locus.getSiteSample(sampleIndex));
        const unsigned total_calls(siteSampleInfo.n_used_calls+siteSampleInfo.n_unused_calls);
        const double unusedCallFraction(safeFrac(siteSampleInfo.n_unused_calls, total_calls));
        if (unusedCallFraction>_opt.max_base_filt) sampleInfo.filters.set(GERMLINE_VARIANT_VCF_FILTERS::HighBaseFilt);
    }
    if (locus.isVariantLocus())
    {
        if (_opt.is_max_snv_sb)
        {
            /// TODO STREL-125 solve strand bias for multi-alt/multi-sample:
#if 0
            if (allele.strandBias>_opt.max_snv_sb) sampleInfo.filters.set(GERMLINE_VARIANT_VCF_FILTERS::HighSNVSB);
#endif
        }
        if (_opt.is_max_snv_hpol)
        {
            if (static_cast<int>(locus.hpol)>_opt.max_snv_hpol) sampleInfo.filters.set(GERMLINE_VARIANT_VCF_FILTERS::HighSNVHPOL);
        }
    }
}



void
ScoringModelManager::
default_classify_site_locus(
    GermlineSiteLocusInfo& locus) const
{
    const unsigned sampleCount(locus.getSampleCount());
    const unsigned allSampleLocusDepth(locus.getTotalReadDepth());
    for (unsigned sampleIndex(0); sampleIndex<sampleCount; ++sampleIndex)
    {
        default_classify_site(sampleIndex, allSampleLocusDepth, locus);
    }
}



void
ScoringModelManager::
default_classify_indel(
    const unsigned sampleIndex,
    const unsigned allSampleLocusDepth,
    GermlineIndelLocusInfo& locus) const
{
    LocusSampleInfo& sampleInfo(locus.getSample(sampleIndex));

    if (_opt.is_min_gqx)
    {
        if (sampleInfo.gqx < _opt.min_gqx) sampleInfo.filters.set(GERMLINE_VARIANT_VCF_FILTERS::LowGQX);
    }

    if (_dopt.is_max_depth())
    {
        if (allSampleLocusDepth > _dopt.max_depth)
            sampleInfo.filters.set(GERMLINE_VARIANT_VCF_FILTERS::HighDepth);
    }

    if (_opt.is_max_ref_rep())
    {
        /// TODO - can this filter be eliminated? Right now it means that any allele in an overlapping allele set
        /// could trigger a filtration of the whole locus b/c of single allele's properties.
        ///
        /// would need unary format to express filtration per allele in a sane way....
        for (const auto& allele : locus.getIndelAlleles())
        {
            const auto& iri(allele.indelReportInfo);
            if (iri.is_repeat_unit())
            {
                if ((iri.repeat_unit.size() <= 2) &&
                    (static_cast<int>(iri.ref_repeat_count) > _opt.max_ref_rep))
                {
                    sampleInfo.filters.set(GERMLINE_VARIANT_VCF_FILTERS::HighRefRep);
                }
            }
        }
    }
}



void
ScoringModelManager::
default_classify_indel_locus(
    GermlineIndelLocusInfo& locus) const
{
    const unsigned sampleCount(locus.getSampleCount());
    const unsigned allSampleLocusDepth(locus.getTotalReadDepth());
    for (unsigned sampleIndex(0); sampleIndex<sampleCount; ++sampleIndex)
    {
        default_classify_indel(sampleIndex, allSampleLocusDepth, locus);
    }
}
