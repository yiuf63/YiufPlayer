// 示例 JavaScript 插件

async function search(keyword, page) {
    return [
        { id: "demo_vid_1", title: "大屏播放器演示视频", type: "video", duration: 180 }
    ];
}

async function loadDetail(id) {
    return {
        id: id,
        title: "大屏播放器演示视频",
        overview: "基于 QuickJS 轻量运行时的脚本插件，适用于手柄与遥控器交互。",
        episodes: [
            { id: "ep1", title: "第一集：跨平台大屏体验", episodeNumber: 1 }
        ]
    };
}

async function loadLinks(episodeId) {
    return {
        streams: [
            {
                name: "1080P 高清演示流",
                url: "https://commondatastorage.googleapis.com/gtv-videos-bucket/sample/BigBuckBunny.mp4",
                format: "mp4",
                headers: {
                    "User-Agent": "DeckPlayer/1.0"
                }
            }
        ]
    };
}

async function listDirectory(path) {
    return {
        directories: [
            { name: "网盘演示目录", path: "/demo" }
        ],
        files: [
            { name: "SampleVideo.mp4", path: "/demo/SampleVideo.mp4", size: 10485760, type: "video" }
        ]
    };
}

async function resolvePlayUrl(path) {
    return {
        url: "https://commondatastorage.googleapis.com/gtv-videos-bucket/sample/BigBuckBunny.mp4",
        name: "演示直链",
        format: "mp4"
    };
}

async function scrapeSearch(query, year) {
    return [
        { id: "sc_demo", title: query, year: year }
    ];
}

async function scrapeDetail(id) {
    return {
        title: "演示影片刮削元数据",
        overview: "通过脚本拉取的影视元数据信息",
        rating: 9.0
    };
}
