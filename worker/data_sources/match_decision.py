"""
最优结果判定模块

实现分权判定逻辑，综合 score 和 title_sim 等多维度指标判定最佳匹配。

核心设计思想：
    - score 是"综合评分"
    - title_sim 是"可信度等级"
    - 最终决策：在某个 title 可信度等级下，score 是否够高

使用方式：
    from data_sources.match_decision import MatchDecision, MatchScore

    decision = MatchDecision(config)
    result = decision.compute_final_score(query, recording, title_sim, artist_sim, duration_score)
    is_best = decision.is_best_match(candidates)
"""

from dataclasses import dataclass, field
from enum import Enum
from typing import Dict, List, Optional, Any
import logging

logger = logging.getLogger(__name__)


class TitleSimLevel(Enum):
    """标题相似度等级
    
    分级标准：
        S: [0.95 ~ 1.00] 几乎完全一致
        A: [0.90 ~ 0.95) 高度匹配
        B: [0.85 ~ 0.90) 可接受
        C: [0.75 ~ 0.85) 风险区
        D: [< 0.75] 拒绝
    """
    S = "S"
    A = "A"
    B = "B"
    C = "C"
    D = "D"


@dataclass
class MatchScore:
    """匹配评分数据
    
    存储单个候选的完整评分信息，用于最终判定。
    
    Attributes:
        base_score: 基础分数 (0-100)
        final_score: 最终分数（含 title 分权）
        title_sim: 标题相似度 (0.0-1.0)
        title_level: 标题相似度等级
        artist_sim: 艺术家相似度 (0.0-1.0)
        duration_score: 时长匹配分数 (0-20)
        raw: 原始数据
    """
    base_score: float = 0.0
    final_score: float = 0.0
    title_sim: float = 0.0
    title_level: TitleSimLevel = TitleSimLevel.D
    artist_sim: float = 0.0
    duration_score: float = 0.0
    raw: Dict[str, Any] = field(default_factory=dict)


@dataclass
class DecisionConfig:
    """判定配置
    
    Attributes:
        title_weight_s: S级增强分数
        title_weight_a: A级增强分数
        title_weight_b: B级增强分数（通常为0）
        title_weight_c: C级惩罚分数
        title_weight_d: D级惩罚分数
        score_threshold: 基础分数阈值
        score_margin: 最佳与次佳分差阈值
        single_s_threshold: 单候选S级直接通过
        single_a_threshold: 单候选A级分数阈值
        single_b_threshold: 单候选B级分数阈值
        single_c_threshold: 单候选C级分数阈值
        multi_c_threshold: 多候选C级分数阈值
    """
    title_weight_s: int = 8
    title_weight_a: int = 5
    title_weight_b: int = 0
    title_weight_c: int = -8
    title_weight_d: int = -20
    
    score_threshold: int = 85
    score_margin: int = 10
    
    single_s_threshold: int = 0
    single_a_threshold: int = 85
    single_b_threshold: int = 88
    single_c_threshold: int = 92
    
    multi_c_threshold: int = 90
    
    title_level_s: float = 0.95
    title_level_a: float = 0.90
    title_level_b: float = 0.85
    title_level_c: float = 0.75


class MatchDecision:
    """最优结果判定器
    
    实现分权判定逻辑，综合 score 和 title_sim 等多维度指标判定最佳匹配。
    
    核心方法：
        classify_title_sim: 标题相似度分级
        apply_title_weight: 应用标题分权
        compute_final_score: 计算最终分数
        is_best_match: 判定是否找到最佳匹配
        is_best_single: 单候选判定
        is_best_multi: 多候选判定
    """
    
    def __init__(self, config: Optional[DecisionConfig] = None):
        """初始化判定器
        
        Args:
            config: 判定配置，为 None 时使用默认配置
        """
        self._config = config or DecisionConfig()
        logger.debug(f"MatchDecision initialized with config: score_threshold={self._config.score_threshold}")
    
    def classify_title_sim(self, title_sim: float) -> TitleSimLevel:
        """标题相似度分级
        
        将 title_sim 映射到 S/A/B/C/D 五个等级。
        
        Args:
            title_sim: 标题相似度 (0.0-1.0)
        
        Returns:
            TitleSimLevel: 标题相似度等级
        """
        if title_sim >= self._config.title_level_s:
            return TitleSimLevel.S
        elif title_sim >= self._config.title_level_a:
            return TitleSimLevel.A
        elif title_sim >= self._config.title_level_b:
            return TitleSimLevel.B
        elif title_sim >= self._config.title_level_c:
            return TitleSimLevel.C
        else:
            return TitleSimLevel.D
    
    def apply_title_weight(self, base_score: float, title_sim: float) -> float:
        """应用标题分权
        
        根据 title_sim 等级对 base_score 进行增强或惩罚。
        
        Args:
            base_score: 基础分数 (0-100)
            title_sim: 标题相似度 (0.0-1.0)
        
        Returns:
            float: 调整后的分数
        """
        level = self.classify_title_sim(title_sim)
        
        if level == TitleSimLevel.S:
            return base_score + self._config.title_weight_s
        elif level == TitleSimLevel.A:
            return base_score + self._config.title_weight_a
        elif level == TitleSimLevel.B:
            return base_score + self._config.title_weight_b
        elif level == TitleSimLevel.C:
            return base_score + self._config.title_weight_c
        else:
            return base_score + self._config.title_weight_d
    
    def compute_final_score(
        self,
        base_score: float,
        title_sim: float,
        artist_sim: float = 0.0,
        duration_score: float = 0.0,
        raw: Optional[Dict[str, Any]] = None
    ) -> MatchScore:
        """计算最终分数
        
        综合基础分数和 title_sim 分权，生成完整的 MatchScore 对象。
        
        Args:
            base_score: 基础分数 (0-100)
            title_sim: 标题相似度 (0.0-1.0)
            artist_sim: 艺术家相似度 (0.0-1.0)
            duration_score: 时长匹配分数 (0-20)
            raw: 原始数据
        
        Returns:
            MatchScore: 完整的评分对象
        """
        title_level = self.classify_title_sim(title_sim)
        final_score = self.apply_title_weight(base_score, title_sim)
        
        return MatchScore(
            base_score=base_score,
            final_score=final_score,
            title_sim=title_sim,
            title_level=title_level,
            artist_sim=artist_sim,
            duration_score=duration_score,
            raw=raw or {}
        )
    
    def is_best_single(self, candidate: Dict[str, Any]) -> bool:
        """单候选判定
        
        判定单个候选是否为高质量匹配。单置信度越低，需要越高综合分数。
        
        判定逻辑：
            - S级：直接通过
            - A级：final_score >= 85
            - B级：final_score >= 88
            - C级：final_score >= 92
            - D级：拒绝
        
        Args:
            candidate: 候选数据（包含 final_score, title_sim, title_level 等字段）
        
        Returns:
            bool: 是否为高质量匹配
        """
        final_score = candidate.get("final_score", 0)
        title_level = candidate.get("title_level", TitleSimLevel.D)
        
        if title_level == TitleSimLevel.S:
            return True
        
        if title_level == TitleSimLevel.A:
            return final_score >= self._config.single_a_threshold
        
        if title_level == TitleSimLevel.B:
            return final_score >= self._config.single_b_threshold
        
        if title_level == TitleSimLevel.C:
            return final_score >= self._config.single_c_threshold
        
        return False
    
    def is_best_multi(self, candidates: List[Dict[str, Any]]) -> bool:
        """多候选判定
        
        判定候选列表中是否存在高质量匹配。
        
        判定逻辑：
            1. best_score >= score_threshold (默认 85)
            2. best_score - second_score >= score_margin (默认 10)
            3. title_level 不能为 D
            4. C级需要更高的分数阈值
        
        Args:
            candidates: 候选列表（按 final_score 降序排列）
        
        Returns:
            bool: 是否存在高质量匹配
        """
        if len(candidates) < 2:
            return False
        
        best = candidates[0]
        second = candidates[1]
        
        best_score = best.get("final_score", 0)
        second_score = second.get("final_score", 0)
        title_level = best.get("title_level", TitleSimLevel.D)
        
        if best_score < self._config.score_threshold:
            return False
        
        if best_score - second_score < self._config.score_margin:
            return False
        
        if title_level == TitleSimLevel.D:
            return False
        
        if title_level == TitleSimLevel.C:
            if best_score < self._config.multi_c_threshold:
                return False
        
        return True
    
    def is_best_match(self, candidates: List[Dict[str, Any]]) -> bool:
        """统一入口：判定是否找到最佳匹配
        
        根据候选数量自动选择单候选或多候选判定逻辑。
        
        Args:
            candidates: 候选列表（包含 final_score, title_sim, title_level 等字段）
        
        Returns:
            bool: 是否找到高质量匹配
        """
        if not candidates:
            return False
        
        sorted_candidates = sorted(
            candidates,
            key=lambda x: x.get("final_score", 0),
            reverse=True
        )
        
        if len(sorted_candidates) == 1:
            result = self.is_best_single(sorted_candidates[0])
            logger.debug(
                f"MatchDecision::is_best_match (single): "
                f"final_score={sorted_candidates[0].get('final_score', 0):.1f}, "
                f"title_level={sorted_candidates[0].get('title_level', TitleSimLevel.D).value}, "
                f"result={result}"
            )
            return result
        
        result = self.is_best_multi(sorted_candidates)
        best = sorted_candidates[0]
        second = sorted_candidates[1]
        logger.debug(
            f"MatchDecision::is_best_match (multi): "
            f"best_score={best.get('final_score', 0):.1f}, "
            f"second_score={second.get('final_score', 0):.1f}, "
            f"title_level={best.get('title_level', TitleSimLevel.D).value}, "
            f"result={result}"
        )
        return result
    
    def is_title_valid(self, title_sim: float) -> bool:
        """判定标题相似度是否有效
        
        硬性约束：title_sim >= 0.75
        
        Args:
            title_sim: 标题相似度 (0.0-1.0)
        
        Returns:
            bool: 是否有效
        """
        return title_sim >= self._config.title_level_c
