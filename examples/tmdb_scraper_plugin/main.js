// Muvik TMDB 影视刮削插件模板
//
// 规范说明：
//   1. 必须导出 scrapeSearch(query, year) 用于根据搜索词和年份返回候选列表
//   2. 必须导出 scrapeDetail(id) 用于根据选中的候选条目 ID 获取完整元数据详情
//   3. 宿主提供 host.http 进行网络请求，无需第三方 fetch / axios 库

const TMDB_BASE = "https://api.themoviedb.org/3";
const TMDB_IMG_BASE = "https://image.tmdb.org/t/p/w500";

/**
 * 搜索影视候选列表
 * @param {string} query 清洗后的片名关键词（如 "奥本海默" 或 "Oppenheimer"）
 * @param {number} year 年份（可选，若未能从文件名提取则为 0）
 * @returns {Promise<Array<{ id: string, title: string, year: number }>>}
 */
async function scrapeSearch(query, year) {
    const apiKey = host.getConfig("api_key") || "";
    const lang = host.getConfig("language") || "zh-CN";

    if (!apiKey) {
        console.log("[TMDB] 未配置 api_key，返回本地基础演示条目");
        return [
            { id: "demo_oppenheimer", title: query || "示例电影", year: year || 2023 }
        ];
    }

    try {
        let url = `${TMDB_BASE}/search/multi?api_key=${encodeURIComponent(apiKey)}&query=${encodeURIComponent(query)}&language=${encodeURIComponent(lang)}`;
        if (year > 1900) {
            url += `&year=${year}`;
        }

        const resp = host.http({
            url: url,
            method: "GET",
            timeout: 10
        });

        if (resp.status !== 200) {
            console.error(`[TMDB] 搜索请求失败: HTTP ${resp.status}`);
            return [];
        }

        const data = JSON.parse(resp.body);
        if (!data.results || !Array.isArray(data.results)) {
            return [];
        }

        return data.results
            .filter(item => item.media_type === "movie" || item.media_type === "tv")
            .map(item => {
                const title = item.title || item.name || query;
                const releaseDate = item.release_date || item.first_air_date || "";
                const parsedYear = releaseDate ? parseInt(releaseDate.substring(0, 4), 10) : 0;
                return {
                    id: `${item.media_type}:${item.id}`,
                    title: title,
                    year: parsedYear
                };
            });
    } catch (err) {
        console.error(`[TMDB] 搜索发生异常: ${err}`);
        return [];
    }
}

/**
 * 获取具体影视的详情信息
 * @param {string} id 候选条目的全局 ID（如 "movie:872585"）
 * @returns {Promise<{
 *   title: string,
 *   overview: string,
 *   release_date?: string,
 *   runtime_minutes?: number,
 *   rating?: number,
 *   genres?: string,
 *   poster_url?: string
 * }>}
 */
async function scrapeDetail(id) {
    const apiKey = host.getConfig("api_key") || "";
    const lang = host.getConfig("language") || "zh-CN";

    // 演示回退数据
    if (!apiKey || id === "demo_oppenheimer") {
        return {
            title: "奥本海默",
            overview: "克里斯托弗·诺兰自编自导的传记片，讲述美国物理学家 J·罗伯特·奥本海默主导制造原子弹的故事。",
            release_date: "2023-07-21",
            runtime_minutes: 180,
            rating: 8.9,
            genres: "传记, 剧情, 历史",
            poster_url: "https://image.tmdb.org/t/p/w500/8Gxv8gSFCU0XGDykEGv7zR1n2ua.jpg"
        };
    }

    try {
        const parts = id.split(":");
        const mediaType = parts[0] === "tv" ? "tv" : "movie";
        const tmdbId = parts[1] || id;

        const url = `${TMDB_BASE}/${mediaType}/${tmdbId}?api_key=${encodeURIComponent(apiKey)}&language=${encodeURIComponent(lang)}`;
        const resp = host.http({
            url: url,
            method: "GET",
            timeout: 10
        });

        if (resp.status !== 200) {
            console.error(`[TMDB] 详情请求失败: HTTP ${resp.status}`);
            return null;
        }

        const data = JSON.parse(resp.body);
        const title = data.title || data.name || "";
        const overview = data.overview || "";
        const releaseDate = data.release_date || data.first_air_date || "";
        const runtime = data.runtime || (data.episode_run_time && data.episode_run_time[0]) || 0;
        const rating = data.vote_average || 0;
        const genres = Array.isArray(data.genres) ? data.genres.map(g => g.name).join(", ") : "";
        const posterUrl = data.poster_path ? `${TMDB_IMG_BASE}${data.poster_path}` : "";

        return {
            title: title,
            overview: overview,
            release_date: releaseDate,
            runtime_minutes: runtime,
            rating: rating,
            genres: genres,
            poster_url: posterUrl
        };
    } catch (err) {
        console.error(`[TMDB] 获取详情发生异常: ${err}`);
        return null;
    }
}
