var tt =0;
var th = 0;

function updateStatus() {
    $.ajax({
      method: "GET",
      url: window.location.origin+"/get/status",
      success: function(data) {
        $('.temp-value').empty().append(data.tval);
        $('.humid-value').empty().append(data.hval);
        tt=data.ttarget;
        th = data.htarget;
        $('.status-box').append(data.status);
        $('.status-box').scrollTop(99999);
      }
    });
    setTimeout(updateStatus, 2000); // you could choose not to continue on failure...
}
  
function submitTarget()
{
  var tobj = new Object();
  tobj.thum = $('.humid-target').val;
  tobj.ttemp = $('.temp-target').val;
  $.ajax({
    method: "PUT",
    url: window.location.origin+"/update/target",
    data: tobj,
    fail: function(data){
      $('.temp-target').val(data.ttarget);
      $('.humid-target').val(data.htarget);
    }
  });
}

$(document).ready(function() {
    $('.submit-target').onclick = submitTarget;
    updateStatus();
    $('.temp-target').val(tt);
    $('.humid-target').val(th);
});
