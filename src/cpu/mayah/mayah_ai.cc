#include "mayah_ai.h"

#include <iostream>
#include <sstream>

#include <gflags/gflags.h>

#include "base/time.h"
#include "core/algorithm/plan.h"
#include "core/algorithm/puyo_possibility.h"
#include "core/frame_data.h"

#include "book_field.h"
#include "book_reader.h"
#include "evaluator.h"
#include "feature_parameter.h"
#include "gazer.h"

DEFINE_string(feature, SRC_DIR "/cpu/mayah/feature.txt", "the path to feature parameter");
DEFINE_string(book, SRC_DIR "/cpu/mayah/book.txt", "the path to book");
DEFINE_bool(log_max_score, false, "log max score to stderr");

using namespace std;

MayahAI::MayahAI(int argc, char* argv[]) :
    AI(argc, argv, "mayah")
{
    featureParameter_.reset(new FeatureParameter(FLAGS_feature));
    books_ = BookReader::parse(FLAGS_book);
    LOG(INFO) << featureParameter_->toString();
    for (const auto& bf : books_) {
        LOG(INFO) << bf.toDebugString();
    }
    google::FlushLogFiles(google::INFO);
}

MayahAI::~MayahAI()
{
}

void MayahAI::reloadParameter()
{
    featureParameter_.reset(new FeatureParameter(FLAGS_feature));
}

void MayahAI::gameWillBegin(const FrameData& frameData)
{
    thoughtMaxRensa_ = 0;
    thoughtMaxScore_ = 0;
    gazer_.initialize(frameData.id);
}

void MayahAI::gameHasEnded(const FrameData&)
{
    if (FLAGS_log_max_score) {
        cerr << "max rensa = " << thoughtMaxRensa_ << endl;
        cerr << "max score = " << thoughtMaxScore_ << endl;
    }
}

DropDecision MayahAI::think(int frameId, const PlainField& plainField, const KumipuyoSeq& kumipuyoSeq)
{
    CoreField f(plainField);
    double beginTime = currentTime();
    Plan plan = thinkPlan(frameId, f, kumipuyoSeq, MayahAI::DEFAULT_DEPTH, MayahAI::DEFAULT_NUM_ITERATION);
    double endTime = currentTime();
    std::string message = makeMessageFrom(frameId, f, kumipuyoSeq, MayahAI::DEFAULT_NUM_ITERATION, plan, endTime - beginTime);
    if (plan.decisions().empty())
        return DropDecision(Decision(3, 0), message);
    return DropDecision(plan.decisions().front(), message);
}

DropDecision MayahAI::thinkFast(int frameId, const PlainField& plainField, const KumipuyoSeq& kumipuyoSeq)
{
    CoreField f(plainField);
    double beginTime = currentTime();
    Plan plan = thinkPlan(frameId, f, kumipuyoSeq, MayahAI::DEFAULT_DEPTH, MayahAI::FAST_NUM_ITERATION);
    double endTime = currentTime();
    std::string message = makeMessageFrom(frameId, f, kumipuyoSeq, MayahAI::FAST_NUM_ITERATION, plan, endTime - beginTime);
    if (plan.decisions().empty())
        return DropDecision(Decision(3, 0), message);
    return DropDecision(plan.decisions().front(), message);
}

Plan MayahAI::thinkPlan(int frameId, const CoreField& field, const KumipuyoSeq& kumipuyoSeq, int depth, int maxIteration)
{
    LOG(INFO) << "\n" << field.toDebugString() << "\n" << kumipuyoSeq.toString();

    int bestRensaScore = 0;
    int bestVirtualRensaScore = 0;
    Plan bestRensaPlan;

    double bestScore = -100000000.0;
    Plan bestPlan;
    auto f = [&, this, frameId, maxIteration](const RefPlan& plan) {
        EvalResult evalResult = Evaluator(*featureParameter_, books_).eval(plan, field, frameId, maxIteration, gazer_);
        if (bestScore < evalResult.score()) {
            bestScore = evalResult.score();
            bestPlan = plan.toPlan();
        }

        if (bestVirtualRensaScore < evalResult.maxVirtualScore()) {
            bestVirtualRensaScore = evalResult.maxVirtualScore();
        }

        if (bestRensaScore < plan.score()) {
            bestRensaScore = plan.score();
            bestRensaPlan = plan.toPlan();
        }

        thoughtMaxScore_ = max(thoughtMaxScore_, plan.score());
        thoughtMaxRensa_ = max(thoughtMaxRensa_, plan.chains());
    };

    Plan::iterateAvailablePlans(field, kumipuyoSeq, depth, f);
    if (bestVirtualRensaScore < bestRensaScore) {
        return bestRensaPlan;
    }
    return bestPlan;
}

std::string MayahAI::makeMessageFrom(int frameId, const CoreField& field, const KumipuyoSeq& kumipuyoSeq, int maxIteration,
                                     const Plan& plan, double thoughtTimeInSeconds) const
{
    UNUSED_VARIABLE(kumipuyoSeq);

    if (plan.decisions().empty())
        return string("give up :-(");

    RefPlan refPlan(plan.field(), plan.decisions(), plan.rensaResult(), plan.numChigiri(), plan.initiatingFrames(), plan.lastDropFrames());
    CollectedFeature cf = Evaluator(*featureParameter_, books_).evalWithCollectingFeature(refPlan, field, frameId, maxIteration, gazer_);

    stringstream ss;
    if (cf.feature(STRATEGY_ZENKESHI) > 0)
        ss << "ZENKESHI / ";
    if (cf.feature(STRATEGY_TAIOU) > 0)
        ss << "TAIOU / ";
    if (cf.feature(STRATEGY_LARGE_ENOUGH) > 0)
        ss << "LARGE_ENOUGH / ";
    if (cf.feature(STRATEGY_TSUBUSHI) > 0)
        ss << "TSUBUSHI / ";
    if (cf.feature(STRATEGY_SAISOKU) > 0)
        ss << "SAISOKU / ";
    else if (cf.feature(STRATEGY_SAKIUCHI) > 0)
        ss << "SAKIUCHI / ";
    if (cf.feature(STRATEGY_HOUWA) > 0)
        ss << "HOUWA / ";

    if (!cf.bookName().empty())
        ss << cf.bookName() << " / ";

    ss << "SCORE = " << cf.score() << " / ";

    if (!cf.feature(MAX_CHAINS).empty()) {
        const vector<int>& vs = cf.feature(MAX_CHAINS);
        for (size_t i = 0; i < vs.size(); ++i)
            ss << "MAX CHAIN = " << vs[i] << " / ";
    }

    if (gazer_.isRensaOngoing()) {
        ss << "Gazed ongoing rensa : " << gazer_.ongoingRensaInfo().rensaResult.score
           << " in " << (gazer_.ongoingRensaInfo().finishingRensaFrameId - frameId) << " / ";
    } else {
        ss << "Gazed max score = "
           << gazer_.estimateMaxScore(frameId + refPlan.totalFrames())
           << " in " << refPlan.totalFrames() << " / "
           << gazer_.estimateMaxScore(frameId + refPlan.totalFrames() + 100)
           << " in " << (refPlan.totalFrames() + 100) << " / "
           << gazer_.estimateMaxScore(frameId + refPlan.totalFrames() + 200)
           << " in " << (refPlan.totalFrames() + 200) << " / ";
    }

    ss << (thoughtTimeInSeconds * 1000) << " [ms]";

    return ss.str();
}

void MayahAI::enemyGrounded(const FrameData& frameData)
{
    // --- Check if Rensa starts.
    CoreField field(frameData.enemyPlayerFrameData().field);
    field.forceDrop();

    RensaResult rensaResult = field.simulate();

    if (rensaResult.chains > 0) {
        requestReconsider();
        gazer_.setOngoingRensa(OngoingRensaInfo(rensaResult, frameData.id + rensaResult.frames));
    } else {
        gazer_.unsetOngoingRensa();
    }
}

void MayahAI::enemyNext2Appeared(const FrameData& frameData)
{
    // At the beginning of the game, kumipuyoSeq might contain EMPTY/EMPTY.
    // In that case, we need to skip.
    const KumipuyoSeq& seq = frameData.enemyPlayerFrameData().kumipuyoSeq;
    if (!isNormalColor(seq.axis(0)) || !isNormalColor(seq.child(0)))
        return;

    gazer_.gaze(frameData.id, frameData.enemyPlayerFrameData().field, frameData.enemyPlayerFrameData().kumipuyoSeq);

    LOG(INFO) << '\n' << gazer_.toRensaInfoString();
}
