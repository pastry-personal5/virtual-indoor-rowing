"""Approved OSM acquisition boundary for the full Han River 5 km review course.

Coordinates are recorded in both the owner-supplied W/E/N/S order and the
``bbox_wsen`` order required by the deterministic OSM acquisition pipeline.
This is geographic source scope only; it does not alter the authored 5,000 m
waterline, course distance, or runtime map.
"""

from __future__ import annotations


FULL_5K_BBOX_WESN = {
	"west": 126.93634,
	"east": 127.00000,
	"north": 37.53100,
	"south": 37.50300,
}

# Scripts/osm_han_area.py accepts west, south, east, north.
FULL_5K_BBOX_WSEN = (
	FULL_5K_BBOX_WESN["west"],
	FULL_5K_BBOX_WESN["south"],
	FULL_5K_BBOX_WESN["east"],
	FULL_5K_BBOX_WESN["north"],
)
