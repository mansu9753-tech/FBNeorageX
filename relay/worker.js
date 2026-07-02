// FBNeoRageX Relay — Cloudflare Worker (Durable Objects 기반)
//
//  배포: workers.cloudflare.com / wrangler deploy
//
//  ★ KV 버전(이전)의 문제
//   ─────────────────────────────────────────────────────────────
//    Cloudflare Workers KV 는 eventual consistency. PUT 후 다른 edge
//    worker 에서 GET 하면 최대 60초 stale cache(null) 를 받음.
//    매치메이킹(짧은 시간 내 양방향 조회)에는 부적합 → 호스트가
//    클라이언트 등록 정보를 끝까지 못 찾는 현상 발생.
//
//  ★ Durable Objects 의 장점
//   ─────────────────────────────────────────────────────────────
//    룸코드별로 unique single instance + strong consistency.
//    PUT 후 즉시 GET 으로 최신값 조회 가능 → 매치메이킹에 정확히 적합.
//    무료 플랜 포함 (SQLite-backed storage, new_sqlite_classes).
//
//  ★ REST 인터페이스는 동일하게 유지 → 클라이언트 코드 변경 불필요.
//
//  배포 절차 (사용자 작업):
//    cd relay
//    wrangler deploy
//  ※ 처음 배포 시 새 DO 클래스 마이그레이션(`new_sqlite_classes`)이
//    자동 실행됨 → 무료 플랜에서 그대로 사용 가능.

const CORS = {
    "Access-Control-Allow-Origin":  "*",
    "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
    "Access-Control-Allow-Headers": "Content-Type",
};

const ROOM_TTL_MS = 120 * 1000;  // 룸 정보 자동 만료 (2분)

// ═════════════════════════════════════════════════════════════
//   Durable Object: RoomDO
//   룸코드 단위 단일 인스턴스. host/client 정보를 in-memory 보관.
//   strong consistency 보장.
// ═════════════════════════════════════════════════════════════
export class RoomDO {
    constructor(state /*, env */) {
        this.state  = state;
        this.host   = null;   // { ip, port, expiresAt } 또는 null
        this.client = null;
        this.loaded = false;
    }

    // 첫 요청 시 storage 에서 상태 복구 (DO hibernation 대응)
    async ensureLoaded() {
        if (this.loaded) return;
        this.host   = await this.state.storage.get("host")   || null;
        this.client = await this.state.storage.get("client") || null;
        this.loaded = true;
    }

    // 만료된 항목 정리
    cleanExpired() {
        const now = Date.now();
        if (this.host   && this.host.expiresAt   < now) this.host   = null;
        if (this.client && this.client.expiresAt < now) this.client = null;
    }

    // peer 정보 추출 (외부 응답용 — expiresAt 숨김)
    peerOf(role) {
        const other = role === "host" ? this.client : this.host;
        if (!other) return null;
        return { ip: other.ip, port: other.port };
    }

    async fetch(request) {
        await this.ensureLoaded();
        this.cleanExpired();

        const url = new URL(request.url);

        // POST /  body: { role, ip, port }  → 등록 + peer 즉시 반환
        if (request.method === "POST") {
            const body = await request.json();
            const { role, ip, port } = body;
            if ((role !== "host" && role !== "client") || !ip || !port)
                return Response.json({ error: "bad fields" }, { status: 400 });

            const entry = { ip, port, expiresAt: Date.now() + ROOM_TTL_MS };
            if (role === "host") {
                this.host = entry;
                await this.state.storage.put("host", entry);
            } else {
                this.client = entry;
                await this.state.storage.put("client", entry);
            }
            return Response.json({ ok: true, peer: this.peerOf(role) });
        }

        // GET /<role>  → 해당 role 정보 조회
        if (request.method === "GET") {
            const role  = url.pathname.replace(/^\//, "");
            const entry = role === "host"   ? this.host
                        : role === "client" ? this.client
                        : null;
            if (!entry) return Response.json({ found: false });
            return Response.json({
                found: true,
                ip:    entry.ip,
                port:  entry.port,
            });
        }

        return new Response("RoomDO: method not allowed", { status: 405 });
    }
}

// ═════════════════════════════════════════════════════════════
//   메인 워커 — REST 라우팅 → DO 호출
// ═════════════════════════════════════════════════════════════
export default {
    async fetch(request, env) {
        if (request.method === "OPTIONS")
            return new Response(null, { status: 204, headers: CORS });

        const url  = new URL(request.url);
        const path = url.pathname;

        // ── POST /room  Body: { code, role, ip, port } ──────
        if (request.method === "POST" && path === "/room") {
            let body;
            try { body = await request.json(); }
            catch { return Response.json({ error: "invalid json" }, { status: 400, headers: CORS }); }

            const { code, role, ip, port } = body;
            if (!code || !role || !ip || !port)
                return Response.json({ error: "missing fields" }, { status: 400, headers: CORS });

            // 룸코드 → DO 인스턴스 ID (deterministic)
            const id   = env.ROOMS.idFromName(code);
            const stub = env.ROOMS.get(id);
            const res  = await stub.fetch("https://do/", {
                method:  "POST",
                headers: { "Content-Type": "application/json" },
                body:    JSON.stringify({ role, ip, port }),
            });
            const data = await res.json();
            return Response.json(data, { headers: CORS });
        }

        // ── GET /room/:code/:role  → peer 정보 조회 ─────────
        if (request.method === "GET" && path.startsWith("/room/")) {
            const parts = path.split("/");  // ["", "room", code, role]
            if (parts.length < 4)
                return Response.json({ error: "usage: /room/:code/:role" }, { status: 400, headers: CORS });

            const code = parts[2];
            const role = parts[3];

            const id   = env.ROOMS.idFromName(code);
            const stub = env.ROOMS.get(id);
            const res  = await stub.fetch(`https://do/${role}`, { method: "GET" });
            const data = await res.json();
            return Response.json(data, { headers: CORS });
        }

        return new Response("FBNeoRageX Relay (Durable Objects) OK", { headers: CORS });
    },
};
