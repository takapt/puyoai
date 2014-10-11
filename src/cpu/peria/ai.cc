#include "cpu/peria/ai.h"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <algorithm>
#include <functional>  // C++11
#include <map>
#include <sstream>
#include <vector>

#include "core/algorithm/plan.h"
#include "core/constant.h"
#include "core/field/core_field.h"
#include "core/field/rensa_result.h"
#include "core/frame_request.h"
#include "core/puyo_color.h"
#include "pattern.h"

namespace peria {

namespace {

// Score current field situation.
int PatternMatch(const RefPlan& plan, std::string* name) {
  int sum = 0;
  int best = 0;

  const CoreField& field = plan.field();
  std::ostringstream oss;
  for (const Pattern& pattern : Pattern::GetAllPattern()) {
    int score = pattern.Match(field);
    sum += score;
    if (score > best) {
      best = score;
      oss << " " << pattern.name() << " " << score << "/" << pattern.score();
    }
  }
  *name = oss.str();

  // Debug list
  // LOG(INFO) << *name;
  // LOG(INFO) << "Sum: " << sum;
  // LOG(INFO) << "Num: " << field.countPuyos();
  // LOG(INFO) << "\n" << field.toDebugString();

  return sum;
}

typedef std::map<Decision, std::vector<int> > CandidateMap;

void EvaluateFuture(const RefPlan& plan, int* best_score) {
  int score = plan.rensaResult().score;
  if (score > *best_score)
    *best_score = score;
}

void EvaluateUsual(const RefPlan& plan,
                   int* best_score,
                   Decision* decision) {
  int score = 0;
  if (plan.isRensaPlan()) {
    const RensaResult& result = plan.rensaResult();
    if (result.chains == 1 && result.quick && result.score > 70 * 6 * 2) {
      // Quick attack
      score = 99999;
    } else {
      score = result.score;
    }
  } else {
    Plan::iterateAvailablePlans(
        plan.field(), KumipuyoSeq(), 1,
        std::bind(EvaluateFuture, std::placeholders::_1, &score));
    score /= 2;
  }

  if (score > *best_score) {
    *best_score = score;
    *decision = plan.decisions().front();
  }
}

void EvaluatePatterns(const RefPlan& plan,
                      int* best_score,
                      std::string* best_name,
                      int* frames,
                      Decision* decision) {
  // Do not check patterns if puyos vanish.
  if (plan.isRensaPlan())
    return;

  std::string name;
  int score = PatternMatch(plan, &name);
  if (score > *best_score ||
      (score == *best_score && plan.rensaResult().frames < *frames)) {
    *best_score = score;
    *best_name = name;
    *frames = plan.rensaResult().frames;
    *decision = plan.decision(0);
  }
}

void EvaluateCounter(const RefPlan& plan,
                     int threshold,  // Threshold of score.
                     int frame,
                     int* score,
                     Decision* decision) {
  if (!plan.isRensaPlan())
    return;
  if (plan.framesToInitiate() < frame)
    return;
  if (plan.score() < threshold)
    return;
  if (*score > 0 && *score <= plan.score())
    return;

  *score = plan.score();
  *decision = plan.decision(0);
}

}  // namespace

struct Ai::Attack {
  int score;
  int end_frame_id;
};

// TODO: (want to implement)
// - Search decisions for all known |seq|
// --- Count the number of HAKKA-able KumiPuyos
// - Make patterns for JOSEKI.
// --- May be good to score all JOSEKI patterns and evaluate with $\sum score^2$

Ai::Ai(int argc, char* argv[]): ::AI(argc, argv, "peria") {}

Ai::~Ai() {}

DropDecision Ai::think(int frame_id,
                       const PlainField& field,
                       const KumipuyoSeq& seq,
                       const AdditionalThoughtInfo& info) {
  UNUSED_VARIABLE(info);
  using namespace std::placeholders;

  // TODO: Merge all Plan::iterateAvailablePlans() to reduce computing cost.
  
  // Check templates first with visible puyos.
  {
    int score = 0;
    std::string name;
    int frames = 1e+8;
    Decision temp_decision;
    Plan::iterateAvailablePlans(CoreField(field), seq, 2,
                                std::bind(EvaluatePatterns, _1,
                                          &score, &name,
                                          &frames, &temp_decision));
    if (score > 200 && !name.empty())
      return DropDecision(temp_decision, "Template: " + name);
  }

  // 3 個以上おじゃまが来てたらカウンターしてみる
  if (attack_ && attack_->score >= SCORE_FOR_OJAMA * 3) {
    // TODO: Adjust |kAcceptablePuyo|.
    const int kAcceptablePuyo = 3;
    int threshold = attack_->score - SCORE_FOR_OJAMA * kAcceptablePuyo;

    int score = 0;
    Decision decision;
    Plan::iterateAvailablePlans(
        CoreField(field), seq, 2,
        std::bind(EvaluateCounter, _1,
                  threshold,
                  attack_->end_frame_id - frame_id,
                  &score, &decision));
    if (threshold < score)
      return DropDecision(decision, "Counter");
  }

  // Default search
  {
    int score = 0;
    Decision decision;
    Plan::iterateAvailablePlans(
        CoreField(field), seq, 2,
        std::bind(EvaluateUsual, _1, &score, &decision));

    return DropDecision(decision, "Normal");
  }

  // DO NOT REACH HERE
}

void Ai::onGameWillBegin(const FrameRequest& /*frame_request*/) {
  attack_.reset();
}

void Ai::onEnemyGrounded(const FrameRequest& frame_request) {
  const PlainField& enemy = frame_request.enemyPlayerFrameRequest().field;
  CoreField field(enemy);
  field.forceDrop();
  RensaResult result = field.simulate();

  if (result.chains == 0) {
    // TODO: Check required puyos to start RENSA.
    attack_.reset();
    return;
  }

  attack_.reset(new Attack);
  attack_->score = result.score;
  attack_->end_frame_id = frame_request.frameId + result.frames;
}

}  // namespace peria
