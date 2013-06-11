BEGIN;

SELECT evergreen.upgrade_deps_block_check('XXXX', :eg_version);

UPDATE permission.perm_list
SET description = 'Allows a user to process and verify URLs'
WHERE code = 'URL_VERIFY';

COMMIT;
