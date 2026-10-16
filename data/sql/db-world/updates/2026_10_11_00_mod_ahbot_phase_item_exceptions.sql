-- AHBot V2 — GPA item exceptions
-- Apply only to an explicitly selected world database.
-- Do not use against production without authorization.

CREATE TABLE IF NOT EXISTS
mod_ahbot_phase_item_exceptions (
    item_id INT UNSIGNED NOT NULL,
    minimum_phase INT UNSIGNED NOT NULL,
    PRIMARY KEY (item_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

INSERT INTO mod_ahbot_phase_item_exceptions
    (item_id, minimum_phase)
VALUES
    (38923, 70),
    (38948, 70),
    (38986, 80),
    (39004, 80),
    (39006, 80),
    (44493, 80),
    (46026, 80)
ON DUPLICATE KEY UPDATE
    minimum_phase = VALUES(minimum_phase);
