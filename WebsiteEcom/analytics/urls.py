"""
Analytics app URL configuration.
Mounted at /_analytics/ in pradize/urls.py.
"""

from django.urls import path

from . import views

urlpatterns = [
    path('beacon/', views.beacon, name='analytics-beacon'),
]
