package org.emulator.seventy.one;

import android.app.Dialog;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatDialogFragment;
import androidx.appcompat.widget.Toolbar;


public class PortSettingsFragment extends AppCompatDialogFragment {
    private static final String TAG = "PortSettings";
    private boolean debug = false;

    public PortSettingsFragment() {
    }

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        //setStyle(AppCompatDialogFragment.STYLE_NO_FRAME, android.R.style.Theme_Holo_Light);
        setStyle(AppCompatDialogFragment.STYLE_NO_FRAME, android.R.style.Theme_Material);
    }

//    @Override
//    public void onResume() {
//        super.onResume();
//        ViewGroup.LayoutParams params = getDialog().getWindow().getAttributes();
//        params.width = ViewGroup.LayoutParams.MATCH_PARENT;
//        params.height = ViewGroup.LayoutParams.MATCH_PARENT;
//        getDialog().getWindow().setAttributes((android.view.WindowManager.LayoutParams) params);
//    }

    @NonNull
    @Override
    public Dialog onCreateDialog(Bundle savedInstanceState) {
        Dialog dialog = super.onCreateDialog(savedInstanceState);
        Window window = dialog.getWindow();
        if(window != null)
            window.requestFeature(Window.FEATURE_NO_TITLE);
        return dialog;
    }

    @Override
    public View onCreateView(@NonNull LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState) {

        String title = getString(R.string.dialog_port_configuration_title);
        Dialog dialog = getDialog();
        if(dialog != null)
            dialog.setTitle(title);

        // Inflate the layout for this fragment
        View view = inflater.inflate(R.layout.fragment_port_settings, container, false);

        Toolbar toolbar = view.findViewById(R.id.my_toolbar);
        toolbar.setTitle(title);
        toolbar.setNavigationIcon(R.drawable.ic_keyboard_backspace_white_24dp);
        toolbar.setNavigationOnClickListener(
                v -> dismiss()
        );

        return view;
    }
}
