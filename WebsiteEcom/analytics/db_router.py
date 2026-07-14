"""
Analytics DB router (ADR-004).

Routes all models whose app_label is 'analytics' to the 'analytics' database alias.
All other models use 'default'.

Cross-DB foreign keys are explicitly forbidden: analytics models use soft integer IDs
(no FK constraints into the main DB). allow_relation() enforces this boundary.
"""


class AnalyticsRouter:
    ANALYTICS_APPS = {'analytics'}

    def db_for_read(self, model, **hints):
        if model._meta.app_label in self.ANALYTICS_APPS:
            return 'analytics'
        return None

    def db_for_write(self, model, **hints):
        if model._meta.app_label in self.ANALYTICS_APPS:
            return 'analytics'
        return None

    def allow_relation(self, obj1, obj2, **hints):
        # Relations within the same DB boundary are allowed;
        # cross-DB relations (analytics ↔ main) are never allowed.
        obj1_is_analytics = obj1._meta.app_label in self.ANALYTICS_APPS
        obj2_is_analytics = obj2._meta.app_label in self.ANALYTICS_APPS
        if obj1_is_analytics == obj2_is_analytics:
            return True
        return False

    def allow_migrate(self, db, app_label, model_name=None, **hints):
        if app_label in self.ANALYTICS_APPS:
            return db == 'analytics'
        return db == 'default'
