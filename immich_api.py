#!/usr/bin/env python3
import os
import random
import logging
import requests
import settings

logger = logging.getLogger(__name__)

CACHE_DIR = os.path.join(settings.UPLOADS_DIR, 'immich_cache')

def get_favorite_assets(server_url, api_key, timeout=10):
    """Immich 서버에서 즐겨찾기(isFavorite=True) 사진 목록을 가져옵니다."""
    if not server_url or not api_key:
        return []
    
    url = f"{server_url.rstrip('/')}/api/search/metadata"
    headers = {
        "x-api-key": api_key,
        "Content-Type": "application/json"
    }
    payload = {
        "isFavorite": True,
        "type": "IMAGE"
    }
    
    try:
        res = requests.post(url, headers=headers, json=payload, timeout=timeout)
        if res.status_code == 200:
            data = res.json()
            assets = data.get('assets', {}).get('items', [])
            logger.info(f"✅ Immich 즐겨찾기 사진 {len(assets)}장 검색 완료")
            return assets
        else:
            logger.warning(f"⚠️ Immich API 응답 오류: {res.status_code} - {res.text[:100]}")
    except Exception as e:
        logger.warning(f"⚠️ Immich 서버 연결 실패 (오프라인 모드 전환): {e}")
    return []

def download_asset_thumbnail(server_url, api_key, asset_id, cache_dir=CACHE_DIR, timeout=15):
    """선택된 Asset의 고화질 썸네일(preview)을 다운로드하여 캐시에 저장합니다."""
    os.makedirs(cache_dir, exist_ok=True)
    cache_path = os.path.join(cache_dir, f"{asset_id}.jpg")
    
    # 이미 캐시되어 있으면 바로 반환
    if os.path.exists(cache_path) and os.path.getsize(cache_path) > 0:
        logger.info(f"⚡ 캐시된 Immich 사진 사용: {cache_path}")
        return cache_path
    
    url = f"{server_url.rstrip('/')}/api/assets/{asset_id}/thumbnail?size=preview"
    headers = {"x-api-key": api_key}
    
    try:
        res = requests.get(url, headers=headers, timeout=timeout)
        if res.status_code == 200 and len(res.content) > 0:
            with open(cache_path, "wb") as f:
                f.write(res.content)
            logger.info(f"📥 Immich 신규 사진 다운로드 완료: {cache_path} ({len(res.content)//1024} KB)")
            return cache_path
        else:
            logger.warning(f"⚠️ 썸네일 다운로드 실패: {res.status_code}")
    except Exception as e:
        logger.warning(f"⚠️ 썸네일 다운로드 중 에러: {e}")
    return None

def get_random_favorite_photo(config=None):
    """
    Immich에서 즐겨찾기 사진을 랜덤으로 가져옵니다.
    오프라인/연결 실패 시 캐시된 즐겨찾기 사진을 활용합니다.
    """
    if config is None:
        config = settings.load_config()
        
    immich_cfg = config.get("immich", {})
    if not immich_cfg.get("enabled", True):
        return None
        
    server_url = immich_cfg.get("server_url", "http://192.168.0.5:2283")
    api_key = immich_cfg.get("api_key", "")
    
    if not api_key:
        logger.warning("Immich API Key가 설정되지 않았습니다.")
        return None

    os.makedirs(CACHE_DIR, exist_ok=True)

    # 1. 온라인 시도: Immich 서버에서 즐겨찾기 목록 검색
    assets = get_favorite_assets(server_url, api_key)
    if assets:
        # 랜덤 픽 (최대 3회 재시도)
        random_assets = random.sample(assets, min(len(assets), 5))
        for asset in random_assets:
            asset_id = asset.get('id')
            photo_path = download_asset_thumbnail(server_url, api_key, asset_id)
            if photo_path and os.path.exists(photo_path):
                return photo_path

    # 2. 오프라인 폴백: 기존에 다운로드해 둔 캐시 폴더에서 랜덤 선택
    cached_files = [
        os.path.join(CACHE_DIR, f) for f in os.listdir(CACHE_DIR)
        if f.lower().endswith(('.jpg', '.jpeg', '.png')) and os.path.getsize(os.path.join(CACHE_DIR, f)) > 0
    ]
    
    if cached_files:
        chosen = random.choice(cached_files)
        logger.info(f"🎲 오프라인 캐시에서 랜덤 사진 선택: {chosen}")
        return chosen

    logger.warning("사용 가능한 Immich 사진이 없습니다.")
    return None

if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    cfg = {
        "immich": {
            "enabled": True,
            "server_url": "http://192.168.0.5:2283",
            "api_key": "Kk504sSdqaLFdwCRwvO4jVumV0waf1JykAyh9ijTDk"
        }
    }
    photo = get_random_favorite_photo(cfg)
    print("Downloaded/Selected Photo:", photo)
